/* mydaemon ���ޥ��                 */
/* ���߼����Υ��ޥ��                 */
/* send, sendln wait,interrupt ";"    */
/* gcc  mydaemon.c -o mydaemon      */


#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
//#include <sgtty.h>
//#include <sys/termios.h>
#include <termio.h>
#include <signal.h>
#include <stropts.h>
#include <stdlib.h>
#include <strings.h>
#include <stdlib.h>

#define BUFSIZE 2048 
#define TIMEOUT 5

struct termios t_saved, t_termios;
struct winsize wsize;

void settty();
void resettty(int);
int readtty();
int interactive();
void print_usage();
void setwin(int);

size_t strcspn(const char *, const char *);


int debug = 0;
int stdoutput;
int timeout = TIMEOUT;

int
main(int argc, char **argv, char **envp)
{
    int mfd, sfd;
    int i, j;
    int pid;
    int n, m;
    int logfd;
    int cmd_len,valp_len;
    char pty[] = "/dev/ptyp0";    
    char cmd[10];
    char cmds[4][10] = { "wait", "sendln", "send", "interrupt"};
    char *valp;
    char *shell;
    char confbuf[BUFSIZE];
    char buf[BUFSIZE];
    char *config_file;
    char *logfile = "mydaemon.log";
    char default_shell[] = "/bin/sh";
    char *myname;
    struct stat sb;
    char **newenv;

    extern char *ptsname();    

    fd_set rfds;
    FILE *conffp;

    if (argc < 2) {
        print_usage(argv[0]);
        exit(0);
    }


    while ((i = getopt (argc, argv, ":sdt:l:")) != EOF) {
        switch (i){
            case 's':
                stdoutput = 1;
                break;

            case 'd':
                debug = 1;
                break;
                
            case 't':
                timeout = atoi(optarg);
		break;
                
            case 'l':
                logfile = optarg;
		break;                

            default:
                print_usage(argv[0]);
                exit(0);
        }
    }

    config_file =  argv[optind];

    if((logfd = open(logfile, O_WRONLY|O_TRUNC|O_CREAT, 0666)) < 0 ){
        fprintf(stderr, "Can't open log file\n");
        exit(0);
    }
    
    if(( conffp = fopen( config_file, "r")) == 0 ) {
        fprintf(stderr, "Can't open file configuration file\n");
        exit(0);        
    }

    /* �ǽ��ü���� Window ����������¸ */
    ioctl(0, TIOCGWINSZ, &wsize);
    
    if (debug) fprintf(stderr,"config_file = %s \n", config_file);

#ifdef USEPTS
    /*
     * master device �� open
     * ���� ptmx �� open �����������������ޡ��Ȥ��������Τ����ޤ��Ԥ��ʤ�
     */
    if((mfd = open("/dev/ptmx", O_RDWR)) < 0){
        fprintf(stderr, "Cant't open master ptmx\n");
        exit(1);
    }

#else     
    /*
     * ���Ѳ�ǽ�� master device ��õ��
     */
    for (i = 0; i < 8 ; i++ ){
	pty[8] = "pqrsPQRS"[i];
	pty[9] = '0';
	if(stat(pty, &sb) < 0)
            break;
	for (j = 0 ; j < 32 ; j++) {
            pty[9] = "0123456789abcdefghijklmnopqrstuv"[j];
            if ((mfd = open(pty, O_RDWR)) >= 0)
                goto opened;
	}
    }
    fprintf(stderr, "Cant't open master pty\n");
    exit(1);

  opened:        
#endif

    FD_ZERO(&rfds);
    FD_SET(0, &rfds);    
    FD_SET(mfd, &rfds);
    FD_SET(0, &rfds);        
    

    /* ü���ξ���� backup ���Ƥ��� */
//    Old code    
//    ioctl(0, TCGETA, &t_termios);
//    bcopy(&t_termios, &t_saved, sizeof(struct termios));     
    ioctl(0, TCGETS, &t_saved); 

    signal(SIGCLD, resettty); /* shell �� exit ������ˡ�tty ������򸵤��᤹ */
    signal(SIGINT, resettty); /* CTL+C ���������ˡ�tty ������򸵤��᤹ */
    signal(SIGWINCH, setwin); /* �ǽ��ü���Υ�����ɥ����������ѹ����줿 */
    
    if((pid = fork()) < 0){
        perror("fork");
	resettty(0);
        exit(0);
    } else if (pid == 0){ /* �ҥץ����ʥ������*/

        close(logfd);
        fclose(conffp);

#ifdef USEPTS        
        /*
         * ptmx �� ptsname() ��Ȥ���ˡ�����ޤ��Ԥ��ʤ�
         */ 
        char   *slavename;
        grantpt(mfd);
        unlockpt(mfd);
        slavename = ptsname(mfd);

        if ((sfd = open(slavename, O_RDWR)) < 0 ){
            fprintf(stderr, "Can't open slave pts\n");
            exit(1);
        }
        close(mfd);
#else
        
        /*
         * master device /dev/ptyxxx ���б�������/dev/ttyxxx ��õ��
         */
        close(mfd);
	pty[5] = 't'; 
	if ((sfd = open(pty, O_RDWR)) < 0 ){
            fprintf(stderr, "Can't open slave pty\n");
            exit(1);
	}

#endif
           
        /* fork()���� process ����Ω���� process Group ��
         * ����٤ν�����
         * 
         * setsid();
         *
         * ���Υץ����Ǥϼ»ܤ��Ƥ��ʤ���
         * �ޤ�������ȥ���ü��(ps()���ޥ��
         * �Ǹ����Ȥ��� TTY�ˤ򸵤� shell ���鼫ʬ���Ȥ�
         * �ѹ��������
         * 
         *  ioctl(sfd, TIOCSCTTY, (char *)0);
         *
         * ������ϡ�Solaris �� TCOCSCTTY ���ޥ�ɤ򥵥ݡ���
         * ���Ƥ��ʤ��Τǡ��Ȥ��ʤ����ɤ���ä��饳��ȥ���
         * ü�����ѹ��Ǥ���Τ�������
         */
        
	dup2(sfd, 0);
	dup2(sfd, 1);
	dup2(sfd, 2);
	close(sfd);

	if((shell = (char *)getenv("SHELL")) == NULL)
            shell = default_shell;
        
        // �ץ��ץ��ѹ��ΰ٤ν���
        //       putenv("PS1=MYDAEMON: $ ");
        //for ( i = 0 ; envp[i] != NULL ; i++)
        //newenv = (char **)malloc(i + 1);
        //for ( i = 0 ; envp[i] != NULL ; i++){
        //    newenv[i] = envp[i];
        //}
        //newenv[i+1] =

        //execle(shell, shell, NULL, newenv);
        //execl(shell, shell, NULL);
        execle(shell, shell, NULL, envp);
        perror("execle");
        exit(1);
    } /* �� �ץ�������� */
    


    if (debug ) fprintf(stderr,"config file opened successfully\n");
    
    m = mfd + 1;

    while( fgets( confbuf, sizeof(confbuf), conffp) != NULL) {
        


        /* �ԤκǸ�˥�����ʸ�����Ĥ��ä��餽���� terminate */
        if ((valp = strchr(confbuf, ';')) != (char *)NULL)
            *valp = '\0';

        if ((valp = strchr(confbuf, '\n')) != (char *)NULL)
            *valp = '\0';
       
        /* ���Ԥ��ä���롼�פμ��� */ 
        if ((confbuf[0] == '\0') || (confbuf[0] == '\n')) 
            continue;
         
        /* ���֤����ڡ���������ʸ�����Ĺ����׻�*/
        cmd_len = strcspn(confbuf, " \t");
        /* �ͤΥݥ��󥿤�����*/
        valp = confbuf + strcspn(confbuf, "\"") + 1;
        /* �ͤ�Ĺ�������� */
        valp_len = strcspn(valp, "\"");
        /* ���ޥ��������ȴ����� */
        strlcpy(valp, valp, valp_len + 1);
        strlcpy(cmd, confbuf, cmd_len + 1); 

        if(debug){
            fprintf(stderr,"[cmd = \"%s\", valp = \"%s\", cmd_len = %d, valp_len = %d]\n", cmd, 
                    valp, cmd_len, valp_len);
        }

        /* i ����Ӥ��Ƥ�����ͤ� cmds �˴ޤޤ�륳�ޥ�ɤο������ꤷ�ʤ���Фʤ�ʤ� */
	for( i = 0 ; i < 4 ; i++){ 
            if( !strcmp(cmds[i], cmd)){
                switch(i){
                    case 0 : /* wait */                                   
                        readtty(valp, &rfds, mfd, logfd);
                        break;
                                    
                    case 1 : /* sendln */
                        valp = strcat(valp,"\n"); /*�ֲ��ԡפ�����*/
                        /* �񤭹������Ƥ�pty����echo���Ƥ����Τ������Τǡ�����
                           �Ǥ�log�˽񤭹��ޤʤ�*/
                        if(write(mfd,valp,strlen(valp)) < 0 ) {
                            if(errno == EINTR )
                                continue;                
                            perror("write(mfd)");
                            resettty(0);
                            exit(0);                
                        }
                        break;
		    case 2 : /* send */
                        if(write(mfd,valp,strlen(valp)) < 0 ) {
                            if(errno == EINTR )
                                continue;                
                            perror("write(mfd)");
                            resettty(0);
                            exit(0);                
                        }
                        break;
		    case 3 : /* interrupt ���å⡼�ɤء����������ȴ�����ʤ�*/ 

                        interactive(&rfds, mfd, logfd);
                        break;                        
                                    
                    default :
                        break;
                }
            }
        }
    }

    if (debug) fprintf(stderr,"while() end\n");
    
    resettty(0);
    exit(0);
}


/* ɸ�����Ϥ�������ϻ���
 * ü���˷Ҥ��ä� tty �� RAW �⡼�� �ˤ���ECHO �⤷�ʤ�
 */
void
settty ()
{
//    ioctl(0, TCGETA, &t_termios);    
// See http://docs.sun.com/db/doc/806-0636/6j9vq2buk?l=ja&q=ioctl+ONLRET&a=view
// See man termios
// 
//    t_termios.sg_flags |= RAW;
//    t_termios.sg_flags &= ~ECHO;

//    t_termios.c_oflag &= ~OPOST;
//    t_termios.c_lflag &= ~(XCASE | ECHO);
//    t_termios.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
//    t_termios.c_iflag == (IXOFF | IXANY);
//    t_termios.c_lflag |= ~ECHO;    
    
//    t_termios.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
//    t_termios.c_oflag &= ~OPOST;
//    t_termios.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
//    t_termios.c_cflag &= ~(CSIZE|PARENB);
//    t_termios.c_cflag |= CS8;

//    t_termios.c_lflag &= ~(ECHO|ICANON);

    /* ������ main() �� TCGETA �ˤ���¸�����֤��� t_termios ��Ȥ����ɤ��Ϥ����� ��  */
    /* ���Τ����Ԥ�������3���֤��ˤʤäƤ��ޤ���TSGETS �Ǻ��� t_termios �� GET �����*/
    /* �ʲ�������Ǥ��ޤ�������termios ��¤�Τλ������� TCGETA ��Ȥ��٤����ͤ���    */
    /* �ʤˤ���ͳ������Ȼפ��Τ�������                     */
//    ioctl(0, TCGETS, &t_termios);

    struct termios t_termios;
    
    t_termios.c_lflag &= ~ISIG;  /* don't enable signals */
    t_termios.c_lflag &= ~ICANON;/* don't do canonical input */
    t_termios.c_lflag &= ~ECHO;  /* don't echo */
    t_termios.c_iflag &= ~INLCR; /* don't convert nl to cr */
    t_termios.c_iflag &= ~IGNCR; /* don't ignore cr */
    t_termios.c_iflag &= ~ICRNL; /* don't convert cr to nl */
    t_termios.c_iflag &= ~IUCLC; /* don't map upper to lower */
    t_termios.c_iflag &= ~IXON;  /* don't enable ^S/^Q on output */
    t_termios.c_iflag &= ~IXOFF; /* don't enable ^S/^Q on input */
    t_termios.c_oflag &= ~OPOST; /* don't enable output processing */

    ioctl(0, TCSETA, &t_termios);
    return;
}

    

/* backup ���Ƥ�������ü���ξ�����Ȥˤ�ɤ� */
void
resettty(int sig)
{
    if(debug){
        fprintf(stderr,"\rResetting tty(PID:%d)\n\r",getpid());        
        if(t_saved.c_lflag & ISIG) fprintf(stderr, " ISIG ");
        if(t_saved.c_lflag & ICANON) fprintf(stderr, " ICANON ");
        if(t_saved.c_lflag & ECHO) fprintf(stderr, " ECHO ");
        if(t_saved.c_iflag & INLCR) fprintf(stderr, " INLCR ");
        if(t_saved.c_iflag & IGNCR) fprintf(stderr, " IGNCR ");
        if(t_saved.c_iflag & ICRNL) fprintf(stderr, " ICRNL ");
        if(t_saved.c_iflag & IUCLC) fprintf(stderr, " IUCLC ");
        if(t_saved.c_iflag & IXON  ) fprintf(stderr, " IXON   ");
        if(t_saved.c_iflag & IXOFF) fprintf(stderr, " IXOFF ");
        if(t_saved.c_oflag & OPOST) fprintf(stderr, " OPOST ");
        printf ("\n\r");
    }
    ioctl(0, TCSETSW, &t_saved);
    printf("mydaemon done.\n");
    exit(0);
}

/* ���å⡼�� */
int
interactive(fd_set *rfds, int mfd, int logfd)
{
    int n;
    char buf[BUFSIZE];
//    struct winsize wsize;    
    
        /* ü���������echo ̵������*/
    settty();

    if (debug) fprintf(stderr,"Entered interactive mode.\n");

    /*
     * read() ������ǳ����ߤ����ä���ˡ�read() ��
     * block �����Τ��ɤ����ᡢnon-block mode ������
     */      
    if ( fcntl (mfd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl set nonblock");
        exit(0);
    }

    /*
     * window �� size �� ����ü���� master device �ˤ�ȿ�� 
     * ����̵��������̵������ SIGSWINSZ �����Ф����ɤ����⡣��������ä�
     */
    ioctl(mfd, TIOCSWINSZ, &wsize);
    kill(getpid(),SIGWINCH);
    
    for(;;){

        FD_ZERO(rfds);
        FD_SET(0, rfds);
        FD_SET(mfd, rfds);    
        bzero(buf,sizeof(buf));
    
        if( select(mfd + 1, rfds, NULL, NULL, NULL) < 0 ){
            if (errno != EINTR ){
                perror("select");
		resettty(0);
                exit(0);
            }
        }

        if (debug) fprintf(stderr,"\rselect() called in iteractive mode\n");        

            /* ����ü���� master device �������Ԥ� */
        if ( FD_ISSET(mfd, rfds)){

            if ((n = read(mfd, buf, BUFSIZ)) < 0){
                if(errno == EINTR || errno == EAGAIN || errno == EIO )
                    continue;                
                perror("read(mfd)");
                fprintf(stderr,"errno = %d\n",errno);
                      resettty(0);
                      exit(0);
            } else if (n == 0)
                break;
            
            if(debug) fprintf(stderr,"\rfd=3 : %s\n",buf);            
            
            if (write(logfd, buf, n) < 0 ) {
                if(errno == EINTR )
                    continue;                
                perror("write(logfd)");
                resettty(0);
                exit(0);
            }

            if (write(1, buf, n) < 0 ) {
                if(errno == EINTR )
                    continue;                
                perror("write(stdout)");
                resettty(0);
                exit(0);                
            }
        }
            /* ɸ�������Ԥ� */
        if (FD_ISSET(0, rfds)){
            if ((n = read(0, buf, BUFSIZ)) < 0){
                if(errno == EINTR || errno == EAGAIN || errno == EIO)
                    continue;
                perror("read(0)");
                      resettty(0);
                      exit(0);

            } else if (n == 0)
                break;
            if(debug) fprintf(stderr,"\rfd=0 : %s\n",buf);

            if (write(mfd, buf, n) < 0){
                if(errno == EINTR )
                    continue;                
                perror("write(mfd)");
                resettty(0);
                exit(0);
            }

        }
    }
        /* ���� for() ʸ��ȴ���ʤ���ȴ���ơ�
         * config �ե����� �ν������³����
         * �롼������ɲä�˾�ޤ��
         */     
}

int
readtty(char *valp, fd_set *rfds, int mfd, int logfd) 
{
    int n, m;
    char buf[BUFSIZE];
    struct timeval selectwait;

    selectwait.tv_sec = timeout; /* select �Υ����ॢ���� */
    selectwait.tv_usec = 0;

    m = mfd+1;

    FD_ZERO(rfds);
    FD_SET(mfd, rfds);    
        
    for(;;){
        bzero(buf,sizeof(buf));
    
        if((n = select(m, rfds, NULL, NULL, &selectwait)) < 0 ){
            if (errno != EINTR ){
                perror("select");
		resettty(0);
                exit(0);
            }
        }

        if ( n == 0 ){
                fprintf(stderr,"select time out\n");
                resettty(0);
                exit(0);
        }

            /* ����ü���� master device �������Ԥ� */
        if ( FD_ISSET(mfd, rfds)){
            if ((n = read(mfd, buf, BUFSIZ)) < 0){
                perror("read");
                resettty(0);
                exit(0);

            } else if (n == 0)
                break;
            
            if (write(logfd, buf, n) < 0) {
                if(errno == EINTR )
                    continue;                
                perror("write(logfd)");
                resettty(0);
                exit(0);
            }
            if(stdoutput){
                if(write(1, buf, n) < 0){
                    if(errno == EINTR )
                        continue;                
                    perror("write(stdout)");
                    resettty(0);
                    exit(0);                
                }                    
            }
                
            if ( strstr((char *)buf, valp) != NULL ){
                if (debug) fprintf(stderr,"\rreturn to read config file\n");
                return(0);
            }
        }
    }    
}

void
print_usage (char *argv)
{
                printf("Usage: %s [ -sd ][ -t time ] [ -l file ] <config file>\n",argv);
                printf("            -s : Output the logs to STDOUT\n");
                printf("            -d : debug mode\n");
                printf("            -t <time> : select timeout(seconds)\n");
                printf("            -l <filename> : log file name(\"mydaemon.log\" default)\n");
}


size_t
strcspn(const char *string, const char *charset)
{
        const char *p, *q;

        for (q = string; *q != '\0'; ++q) {
                for (p = charset; *p != '\0' && *p != *q; ++p)
                        ;
                if (*p != '\0')
                        break;
        }
        return (q - string);
}

void
setwin(int sig)
{
        /*
         * �ǽ��ü���� Window size �������
         * ����ü���� master device �ˤ�ȿ�Ǥ�����
         * mfd �� 5 �����ꡪ��
         */
//    struct winsize wsize;
    ioctl(0, TIOCGWINSZ, &wsize);
    ioctl(5, TIOCSWINSZ, &wsize);
    if (debug) fprintf(stderr,"\rSIGWINCH catched!\n");
        /* signal ������ */
    signal(SIGCLD, resettty); /* shell �� exit ������ˡ�tty ������򸵤��᤹ */
    signal(SIGINT, resettty); /* CTL+C ���������ˡ�tty ������򸵤��᤹ */
    signal(SIGWINCH, setwin); /* �ǽ��ü���Υ�����ɥ����������ѹ����줿 */    
}

