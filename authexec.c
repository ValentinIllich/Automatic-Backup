#include <qglobal.h>

#include <string.h>
#include <stdlib.h>

#if defined(Q_OS_WIN32)
#include <windows.h>
#endif
#if defined(Q_OS_MAC)
#include <stdio.h>
#endif

void checkForPasswdHelper(int argc, char **argv)
{
#if defined(Q_OS_MAC)
  if( strstr(argv[0],"pwdhelper")!=0 )
  {
    static char buff[128];
    FILE *fp=fopen("/tmp/passwd","r");
    if( fp )
    {
      size_t read = fread(buff,1,127,fp);
      buff[read] = 0x0;
      fclose(fp);
      remove("/tmp/passwd");
    }
    fprintf(stdout,"%s",buff);
    exit(0);
  }
#endif
}

int getAdminRights(int argc, char* argv[],char *password) {
    int result = -1;

    if( argc==2 && strcmp(argv[1],"-admin")==0 )
        return 0;
    else if( argc==1 )
    {
#if defined(Q_OS_WIN32)

        HANDLE child = ShellExecuteA(NULL, "runas", argv[0], "-admin", NULL, SW_SHOWNORMAL);
        if (child) {
          // User accepted UAC prompt (gave permission).
          // The unprivileged parent should wait for
          // the privileged child to finish.
          exit(0);
        }
#endif
#if defined(Q_OS_MAC)
        FILE *fp=fopen("/tmp/passwd","w");
        if( fp )
        {
          fprintf(fp,"%s\n",password);
          fclose(fp);
        }

        char myReadBuffer[4096];
        sprintf(myReadBuffer,"rm -f /tmp/pwdhelper");
        int ret0 = system(myReadBuffer);
        sprintf(myReadBuffer,"ln -s %s /tmp/pwdhelper",argv[0]);
        int ret1 = system(myReadBuffer);

        sprintf(myReadBuffer,"export SUDO_ASKPASS=/tmp/pwdhelper;sudo -A -k %s -admin",argv[0]);
        int ret2 = system(myReadBuffer);
        exit(0);

        result = ret0 + ret1 + ret2;
#endif
    }

    return result;
}
