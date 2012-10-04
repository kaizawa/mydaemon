/* mydaemon コマンド                 */
/* 現在実装のコマンド                 */
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

    /* 最初の端末の Window サイズを保存 */
    ioctl(0, TIOCGWINSZ, &wsize);
    
    if (debug) fprintf(stderr,"config_file = %s \n", config_file);

#ifdef USEPTS
    /*
     * master device を open
     * この ptmx を open するやり方の方がスマートだが、何故かうまく行かない
     */
    if((mfd = open("/dev/ptmx", O_RDWR)) < 0){
        fprintf(stderr, "Cant't open master ptmx\n");
        exit(1);
    }

#else     
    /*
     * 利用可能な master device を探す
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
    

    /* 端末の情報を backup しておく */
//    Old code    
//    ioctl(0, TCGETA, &t_termios);
//    bcopy(&t_termios, &t_saved, sizeof(struct termios));     
    ioctl(0, TCGETS, &t_saved); 

    signal(SIGCLD, resettty); /* shell を exit した後に、tty の設定を元に戻す */
    signal(SIGINT, resettty); /* CTL+C を受けた後に、tty の設定を元に戻す */
    signal(SIGWINCH, setwin); /* 最初の端末のウィンドウサイズが変更された */
    
    if((pid = fork()) < 0){
        perror("fork");
	resettty(0);
        exit(0);
    } else if (pid == 0){ /* 子プロセス（シェル）*/

        close(logfd);
        fclose(conffp);

#ifdef USEPTS        
        /*
         * ptmx と ptsname() を使う方法。うまく行かない
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
         * master device /dev/ptyxxx に対応した　/dev/ttyxxx を探す
         */
        close(mfd);
	pty[5] = 't'; 
	if ((sfd = open(pty, O_RDWR)) < 0 ){
            fprintf(stderr, "Can't open slave pty\n");
            exit(1);
	}

#endif
           
        /* fork()した process を独立した process Group に
         * する為の処理。
         * 
         * setsid();
         *
         * このプログラムでは実施していない。
         * また、コントロール端末(ps()コマンド
         * で見たときの TTY）を元の shell から自分自身へ
         * 変更する処理
         * 
         *  ioctl(sfd, TIOCSCTTY, (char *)0);
         *
         * こちらは、Solaris が TCOCSCTTY コマンドをサポート
         * していないので、使えない。どうやったらコントロール
         * 端末を変更できるのだろうか？
         */
        
	dup2(sfd, 0);
	dup2(sfd, 1);
	dup2(sfd, 2);
	close(sfd);

	if((shell = (char *)getenv("SHELL")) == NULL)
            shell = default_shell;
        
        // プロンプト変更の為の処理
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
    } /* 子 プロセス終わり */
    


    if (debug ) fprintf(stderr,"config file opened successfully\n");
    
    m = mfd + 1;

    while( fgets( confbuf, sizeof(confbuf), conffp) != NULL) {
        


        /* 行の最後にコメント文が見つかったらそこで terminate */
        if ((valp = strchr(confbuf, ';')) != (char *)NULL)
            *valp = '\0';

        if ((valp = strchr(confbuf, '\n')) != (char *)NULL)
            *valp = '\0';
       
        /* 空行だったらループの次へ */ 
        if ((confbuf[0] == '\0') || (confbuf[0] == '\n')) 
            continue;
         
        /* タブかスペースの前の文字列の長さを計算*/
        cmd_len = strcspn(confbuf, " \t");
        /* 値のポインタを得る*/
        valp = confbuf + strcspn(confbuf, "\"") + 1;
        /* 値の長さを得る */
        valp_len = strcspn(valp, "\"");
        /* コマンド部だけ抜き取る */
        strlcpy(valp, valp, valp_len + 1);
        strlcpy(cmd, confbuf, cmd_len + 1); 

        if(debug){
            fprintf(stderr,"[cmd = \"%s\", valp = \"%s\", cmd_len = %d, valp_len = %d]\n", cmd, 
                    valp, cmd_len, valp_len);
        }

        /* i と比較している数値は cmds に含まれるコマンドの数を設定しなければならない */
	for( i = 0 ; i < 4 ; i++){ 
            if( !strcmp(cmds[i], cmd)){
                switch(i){
                    case 0 : /* wait */                                   
                        readtty(valp, &rfds, mfd, logfd);
                        break;
                                    
                    case 1 : /* sendln */
                        valp = strcat(valp,"\n"); /*「改行」を挿入*/
                        /* 書き込み内容はptyからechoしてくるものを取り込むので、ここ
                           ではlogに書き込まない*/
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
		    case 3 : /* interrupt 対話モードへ。一度入ると抜けられない*/ 

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


/* 標準入力からの入力時に
 * 端末に繋がった tty を RAW モード にし、ECHO もしない
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

    /* 本当は main() で TCGETA にて保存して置いた t_termios を使えば良いはずだが 、  */
    /* 何故か改行の送信が3回置きになってしまう。TSGETS で再度 t_termios を GET すると*/
    /* 以下の設定でうまくいく。termios 構造体の時は本来 TCGETA を使うべきの様だが    */
    /* なにか理由があると思うのだが・・                     */
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

    

/* backup しておいた実端末の情報をもとにもどす */
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

/* 対話モード */
int
interactive(fd_set *rfds, int mfd, int logfd)
{
    int n;
    char buf[BUFSIZE];
//    struct winsize wsize;    
    
        /* 端末の設定（echo 無し等）*/
    settty();

    if (debug) fprintf(stderr,"Entered interactive mode.\n");

    /*
     * read() の途中で割り込みが入った後に、read() で
     * block されるのを防ぐため、non-block mode に設定
     */      
    if ( fcntl (mfd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl set nonblock");
        exit(0);
    }

    /*
     * window の size を 仮想端末の master device にも反映 
     * 効果無し・・・無理矢理 SIGSWINSZ を飛ばせば良いかも。。だめだった
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

            /* 仮想端末の master device の入力待ち */
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
            /* 標準入力待ち */
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
        /* この for() 文は抜けない。抜けて、
         * config ファイル の処理を継続する
         * ルーチンの追加が望まれる
         */     
}

int
readtty(char *valp, fd_set *rfds, int mfd, int logfd) 
{
    int n, m;
    char buf[BUFSIZE];
    struct timeval selectwait;

    selectwait.tv_sec = timeout; /* select のタイムアウト */
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

            /* 仮想端末の master device の入力待ち */
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
         * 最初の端末の Window size の設定を
         * 仮想端末の master device にも反映させる
         * mfd を 5 と断定！！
         */
//    struct winsize wsize;
    ioctl(0, TIOCGWINSZ, &wsize);
    ioctl(5, TIOCSWINSZ, &wsize);
    if (debug) fprintf(stderr,"\rSIGWINCH catched!\n");
        /* signal 再設定 */
    signal(SIGCLD, resettty); /* shell を exit した後に、tty の設定を元に戻す */
    signal(SIGINT, resettty); /* CTL+C を受けた後に、tty の設定を元に戻す */
    signal(SIGWINCH, setwin); /* 最初の端末のウィンドウサイズが変更された */    
}

