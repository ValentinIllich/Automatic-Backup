#include <qglobal.h>

#include <string.h>
#include <stdlib.h>

#if defined(Q_OS_WIN32)
#include <windows.h>
#endif
#if defined(Q_OS_MAC)
#include <Security/Authorization.h>
#include <Security/AuthorizationTags.h>
#endif

int getAdminRights(int argc, char* argv[]) {
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
//          WaitForSingleObject(child, INFINITE);
//          CloseHandle(pid);
        }
#endif
#if defined(Q_OS_MAC)
        int read (long,StringPtr,int);
        int write (long,StringPtr,int);

        OSStatus myStatus = 1;
        AuthorizationRef myAuthorizationRef;

        myStatus = AuthorizationCreate(
            NULL,
            kAuthorizationEmptyEnvironment,
            kAuthorizationFlagDefaults,
            &myAuthorizationRef);

        if (myStatus != errAuthorizationSuccess)
            return myStatus;

        do
        {

            {
              AuthorizationItem myItems = {kAuthorizationRightExecute, 0, NULL, 0};
              AuthorizationRights myRights = {1, &myItems};
              AuthorizationFlags myFlags =
                  kAuthorizationFlagDefaults |
                  kAuthorizationFlagInteractionAllowed |
                  kAuthorizationFlagPreAuthorize |
                  kAuthorizationFlagExtendRights;

              myStatus = AuthorizationCopyRights(
                  myAuthorizationRef, &myRights, NULL, myFlags, NULL );
            }

            if (myStatus != errAuthorizationSuccess)
                break;

            {
                FILE *myCommunicationsPipe = NULL;
                unsigned char myReadBuffer[128];

                char *argvs[] = { "-admin",NULL };
                myStatus = AuthorizationExecuteWithPrivileges(
                    myAuthorizationRef,
                    argv[0],
                    kAuthorizationFlagDefaults,
                    &argvs[0],
                    &myCommunicationsPipe);

                if (myStatus == errAuthorizationSuccess)
                    for(;;)
                    {
                        exit(0);
                        int bytesRead = read(fileno(myCommunicationsPipe), myReadBuffer, sizeof(myReadBuffer));
                        if (bytesRead < 1) break;
                        write(fileno(stdout), myReadBuffer, bytesRead);
                    }
            }
        } while (0);

        AuthorizationFree(myAuthorizationRef, kAuthorizationFlagDefaults);

        if (myStatus)
            printf("Status: %ld\n", (long) myStatus);

        result = myStatus;
#endif
    }

    return result;
}
