
// win32 system i/o
//     Startup & Shutdown routines for music,sound,timer,keyboard,...
//     Signal handler to trap errors and exit cleanly.

#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <stdarg.h>

#include "../doomdef.h"
#include "../m_misc.h"
#include "../i_video.h"
#include "../i_sound.h"

#include "../d_net.h"
#include "../g_game.h"

#include "../d_main.h"

#include "../m_argv.h"

#include "../w_wad.h"
#include "../z_zone.h"
#include "../g_input.h"

#include "../keys.h"

#include <mmsystem.h>

#include "win_main.h"
#include "../i_joy.h"

#include <dinput.h>

// Force dinput.h to generate old DX3 headers.
#define DXVERSION_NTCOMPATIBLE  0x0300
//#define LPDIRECTINPUTDEVICE2    LPDIRECTINPUTDEVICE


// ==================
// DIRECT INPUT STUFF
// ==================
static BOOL             bDX0300;        // if true, we created a DirectInput 0x0300 version
                                        // faB: what a beautiful variable name, isn't it ?
LPDIRECTINPUT           lpDI = NULL;
LPDIRECTINPUTDEVICE     lpDIK = NULL;
LPDIRECTINPUTDEVICE     lpDIM = NULL;
LPDIRECTINPUTDEVICE     lpDIJ = NULL;
LPDIRECTINPUTDEVICE2    lpDIJ2 = NULL;

volatile ULONG ticcount;   //returned by I_GetTime(), updated by timer interrupt
int mb_used = 6;



// Do not execute cleanup code more than once. See Shutdown_xxx() routines.
byte    graphics_started=0;
byte    keyboard_started=0;
byte    sound_started=0;
boolean timer_started = false;
boolean mouse_enabled = false;
boolean joystick_detected;


    void    I_AddExitFunc(void (*func)());
    void    I_Quit (void);
    void    I_Error (char *error, ...);
    void    I_OutputMsg (char *error, ...);
    void    I_ShutdownSystem (void);
static void I_ShutdownKeyboard (void);

//
// Force feedback here ? :)
//
void I_Tactile ( int   on,   int   off,   int   total )
{
    // UNUSED.
    on = off = total = 0;
}


//
// Why would this be system specific ?? hmmmm....
//
ticcmd_t        emptycmd;
ticcmd_t*       I_BaseTiccmd(void)
{
    return &emptycmd;
}




//  Allocates the base zone memory,
//  this function returns a valid pointer and size,
//  else it should interrupt the program immediately.
//
//added:11-02-98: now checks if mem could be allocated, this is still
//    prehistoric... there's a lot to do here: memory locking, detection
//    of win95 etc...
//
boolean   win95;
boolean   lockmem;

void I_DetectWin95 (void)
{
    //TODO: get windows version
    
    // Not windows or earlier than Win95
    //      win95 = false;
    
    // 95 or 98 what the hell
    win95 = true;
    
    //added:11-02-98: I'm tired of w95 swapping my memory, so I'm testing
    //                a better way of allocating the mem... still experimental
    //                that's why it's a switch.
    if ( M_CheckParm ("-lock") )
    {
        lockmem = true;
        CONS_Printf ("Memory locking requested.\n");
    }
    else
        lockmem = false;
}


byte* I_ZoneBase (int*  size)
{
    void*      pmem;
    
    // detect the big Bill fake
    I_DetectWin95 ();
    
    // do it the old way
    if (!lockmem)
    {
        *size = mb_used*1024*1024;
        pmem = malloc (*size);
        
        if (!pmem)
        {
            I_Error("Could not allocate %d megabytes.\n"
                "Please use -mb parameter and specify a lower value.\n", mb_used);
        }
        
        //TODO: lock the memory
        memset(pmem,0,*size);
        
        return (byte *) pmem;
    }
    return NULL;
}


void I_GetFreeMem(void)
{
    //TODO: get memory stuff
    /*
    if (info.available_physical_pages != -1)
    {
    CONS_Printf("Available memory         : %6d kb\n", info.available_memory>>10);
    
      CONS_Printf("Available physical memory: %6d kb\n", info.available_physical_pages<<2);
}*/
}


// ---------
// I_Profile
// Two little functions to profile our code using the high resolution timer
// ---------
static LARGE_INTEGER    ProfileCount;
void I_BeginProfile (void)
{
    if (!QueryPerformanceCounter (&ProfileCount))
        //can't profile without the high res timer
        I_Error ("I_BeginProfile FAILED");  
}
//faB: we're supposed to use this to measure very small amounts of time,
//     that's why we return a DWORD and not a 64bit value
DWORD I_EndProfile (void)
{
    LARGE_INTEGER   CurrTime;
    DWORD           ret;
    if (!QueryPerformanceCounter (&CurrTime))
        I_Error ("I_EndProfile FAILED");
    if (CurrTime.QuadPart - ProfileCount.QuadPart > (LONGLONG)0xFFFFFFFFUL)
        I_Error ("I_EndProfile OVERFLOW");
    ret = (DWORD)(CurrTime.QuadPart - ProfileCount.QuadPart);
    // we can call I_EndProfile() several time, I_BeginProfile() need be called just once
    ProfileCount = CurrTime;
    
    return ret;
}


// ---------
// I_GetTime
// Use the High Resolution Timer if available,
// else use the multimedia timer which has 1 millisecond precision on Windowz 95,
// but lower precision on Windowz NT
// ---------
static long    hacktics = 0;       //faB: used locally for keyboard repeat keys
int I_GetTime (void)
{
    int             newtics;
    LARGE_INTEGER   currtime;       // use only LowPart if high resolution counter not available
    static LARGE_INTEGER basetime = {0};

    // use this if High Resolution timer is found
    static LARGE_INTEGER frequency;
    
    if (!basetime.LowPart)
    {
        if (!QueryPerformanceFrequency (&frequency))
            frequency.QuadPart = 0;
        else
            QueryPerformanceCounter (&basetime);
    }

    if (frequency.LowPart &&
        QueryPerformanceCounter (&currtime))
    {
        newtics = (int)((currtime.QuadPart - basetime.QuadPart) * TICRATE / frequency.QuadPart);
    }
    else
    {
        currtime.LowPart = timeGetTime();
        if (!basetime.LowPart)
            basetime.LowPart = currtime.LowPart;
        newtics = ((currtime.LowPart-basetime.LowPart)/(1000/TICRATE));
    }
    hacktics = newtics;    //faB: just a local counter for keyboard repeat key
    return newtics;
}


// should move to i_video
void I_WaitVBL(int count)
{
}


//  Fab: this is probably to activate the 'loading' disc icon
//       it should set a flag, that I_FinishUpdate uses to know
//       whether it draws a small 'loading' disc icon on the screen or not
//
//  also it should explicitly draw the disc because the screen is
//  possibly not refreshed while loading
//
void I_BeginRead (void) {}


//  Fab: see above, end the 'loading' disc icon, set the flag false
//
void I_EndRead (void) {}


byte*   I_AllocLow(int length)
{
    byte*       mem;
        
    mem = (byte *)malloc (length);
    memset (mem,0,length);
    return mem;
}


// ===========================================================================================
//                                                                                      EVENTS
// ===========================================================================================

// ----------
// I_GetEvent
// Post new events for all sorts of user-input
// ----------
static void I_GetKeyboardEvents (void);
static void I_GetMouseEvents (void);
static void I_GetJoystickEvents (void);
void I_GetEvent (void)
{
    I_GetKeyboardEvents ();
    I_GetMouseEvents ();
    I_GetJoystickEvents ();
}


// ----------
// I_StartTic
// ----------
void I_StartTic()
{
    MSG             msg;

    //faB: we need to dispatch messages to the window
    //     so the window procedure can respond to messages and PostEvent() for keys
    //     during D_DoomMain startup.
    if (PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE))
    {
        if (GetMessage( &msg, NULL, 0, 0))
        {
            TranslateMessage(&msg); 
            DispatchMessage(&msg);
        }
    }

    // this is called by the network synchronization,
    // check keys and allow escaping
    I_GetEvent();
}


// ===========================================================================================
//                                                                              TIMER
// ===========================================================================================

//
//  Timer user routine called at ticrate.
//
void winTimer (HWND hwnd, UINT uMsg, UINT idEvent, DWORD dwTime)
{
        ticcount++;
}


//added:08-01-98: we don't use allegro_exit() so we have to do it ourselves.
static  UINT    myTimer;

void I_ShutdownTimer (void)
{
    //if (myTimer)
    //      KillTimer (hWndMain, myTimer);
    timer_started = false;
}


//
//  Installs the timer interrupt handler with timer speed as TICRATE.
//
#define TIMER_ID            1
#define TIMER_RATE          (1000/TICRATE)
void I_StartupTimer(void)
{
    ticcount = 0;
    
    //if ( (myTimer =SetTimer (hWndMain, TIMER_ID, TIMER_RATE, winTimerISR)) == 0)
    //      I_Error("I_StartupTimer: could not install timer.");
    
    //I_AddExitFunc(I_ShutdownTimer);
    timer_started = true;
}


// ===========================================================================================
//                                                                   EXIT CODE, ERROR HANDLING
// ===========================================================================================
extern boolean demorecording;

        int     errorcount = 0;                 // phuck recursive errors
        int shutdowning= false;

//
//  Used to trap various signals, to make sure things get shut down cleanly.
//
static void signal_handler(int num)
{
    static char msg[] = "oh no! back to reality!\r\n";
    char*       sigmsg;
    char        sigdef[64];
        
    I_ShutdownSystem();

    switch (num)
    {
    case SIGINT:
        sigmsg = "interrupt";
        break;
    case SIGILL:
        sigmsg = "illegal instruction - invalid function image";
        break;
    case SIGFPE:
        sigmsg = "floating point exception";
        break;
    case SIGSEGV:
        sigmsg = "segment violation";
        break;
    case SIGTERM:
        sigmsg = "Software termination signal from kill";
        break;
    case SIGBREAK:
        sigmsg = "Ctrl-Break sequence";
        break;
    case SIGABRT:
        sigmsg = "abnormal termination triggered by abort call";
        break;
    default:
        sprintf(sigdef,"signal number %d", num);
        sigmsg = sigdef;
    }
    
    if (logstream != INVALID_HANDLE_VALUE)
    {
        FPrintf (logstream,"signal_handler() error: %s\n", sigmsg);
        CloseHandle (logstream);
        logstream = INVALID_HANDLE_VALUE;
    }
    
    MessageBox(hWndMain,va("signal_handler() : %s",sigmsg),"Doom Legacy error",MB_OK|MB_ICONERROR);
        
    signal(num, SIG_DFL);               //default signal action
    raise(num);
}


//
// put an error message (with format) on stderr
//
void I_OutputMsg (char *error, ...)
{
    va_list     argptr;
    char        txt[512];
        
    va_start (argptr,error);
    vsprintf (txt,error,argptr);
    va_end (argptr);
        
    fprintf (stderr, "Error: %s\n", txt);
    // dont flush the message!

    if (logstream != INVALID_HANDLE_VALUE)
        FPrintf (logstream,"%s", txt);
}


// display error messy after shutdowngfx
//
void I_Error (char *error, ...)
{
    va_list     argptr;
    char        txt[512];

    // added 11-2-98 recursive error detecting
    if(shutdowning)
    {
        errorcount++;
        // try to shutdown separetely eatch stuff
        if(errorcount==5)
            I_ShutdownGraphics();
        if(errorcount==6)
            I_ShutdownSystem();
        if(errorcount==7)
            M_SaveConfig (NULL);
        if(errorcount>20)
        {
            MessageBox(hWndMain,txt,"Doom Legacy Recursive Error",MB_OK|MB_ICONERROR);
            exit(-1);     // recursive errors detected
        }
    }
    shutdowning=true;
        
    // put message to stderr
    va_start (argptr,error);
    wvsprintf (txt, error, argptr);
    va_end (argptr);

    CONS_Printf("I_Error(): %s\n",txt);
    //wsprintf (stderr, "I_Error(): %s\n", txt);
        
    //added:18-02-98: save one time is enough!
    if (!errorcount)
    {
        M_SaveConfig (NULL);   //save game config, cvars..
    }
        
    //added:16-02-98: save demo, could be useful for debug
    //                NOTE: demos are normally not saved here.
    if (demorecording)
        G_CheckDemoStatus();
        
    D_QuitNetGame ();
        
    // shutdown everything that was started !
    I_ShutdownSystem();

    if (logstream != INVALID_HANDLE_VALUE)
    {
        //FPrintf (logstream,"I_Error(): %s", txt);
        CloseHandle (logstream);
        logstream = INVALID_HANDLE_VALUE;
    }
    
    MessageBox (hWndMain, txt, "Doom Legacy Error", MB_OK|MB_ICONERROR);

    //getchar();
    exit (-1);
}


//
// I_Quit : shutdown everything cleanly, in reverse order of Startup.
//
void I_Quit (void)
{
    //added:16-02-98: when recording a demo, should exit using 'q' key,
    //        but sometimes we forget and use 'F10'.. so save here too.
    if (demorecording)
        G_CheckDemoStatus();
        
    M_SaveConfig (NULL);   //save game config, cvars..

    //TODO: do a nice ENDOOM for win32 ?
    //    endoom = W_CacheLumpName("ENDOOM",PU_CACHE);
        
    //added:03-01-98: maybe it needs that the ticcount continues,
    // or something else that will be finished by ShutdownSystem()
    // so I do it before.
    D_QuitNetGame ();
        
    // shutdown everything that was started !
    I_ShutdownSystem();
        
    if (shutdowning || errorcount)
        I_Error("Error detected (%d)",errorcount);

    if (logstream != INVALID_HANDLE_VALUE)
    {
        FPrintf (logstream,"I_Quit(): end of logstream.\n");
        CloseHandle (logstream);
        logstream = INVALID_HANDLE_VALUE;
    }

    // cause an error to test the exception handler
    //i = *((unsigned long *)0x181596);

    fflush(stderr);
    exit(0);
}


// --------------------------------------------------------------------------
// I_ShowLastError
// Displays a GetLastError() error message in a MessageBox
// --------------------------------------------------------------------------
void I_GetLastErrorMsgBox (void)
{
    LPVOID lpMsgBuf;

    FormatMessage( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPTSTR) &lpMsgBuf,
        0,
        NULL 
    );

    // Display the string.
    MessageBox( NULL, lpMsgBuf, "GetLastError", MB_OK|MB_ICONINFORMATION );

    // Free the buffer.
    LocalFree( lpMsgBuf );
}


// ===========================================================================================
// CLEAN STARTUP & SHUTDOWN HANDLING, JUST CLOSE EVERYTHING YOU OPENED.
// ===========================================================================================
//
//
#define MAX_QUIT_FUNCS     16

typedef void (*quitfuncptr)();

static quitfuncptr quit_funcs[MAX_QUIT_FUNCS] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };


//  Adds a function to the list that need to be called by I_SystemShutdown().
//
void I_AddExitFunc(void (*func)())
{
    int c;
    
    for (c=0; c<MAX_QUIT_FUNCS; c++) {
        if (!quit_funcs[c]) {
            quit_funcs[c] = func;
            break;
        }
    }
}


//  Removes a function from the list that need to be called by I_SystemShutdown().
//
void I_RemoveExitFunc(void (*func)())
{
    int c;
    
    for (c=0; c<MAX_QUIT_FUNCS; c++) {
        if (quit_funcs[c] == func) {
            while (c<MAX_QUIT_FUNCS-1) {
                quit_funcs[c] = quit_funcs[c+1];
                c++;
            }
            quit_funcs[MAX_QUIT_FUNCS-1] = NULL;
            break;
        }
    }
}


// ===========================================================================================
// DIRECT INPUT HELPER CODE
// ===========================================================================================


// Create a DirectInputDevice interface,
// create a DirectInputDevice2 interface if possible
static void CreateDevice2 ( LPDIRECTINPUT   di,
                            REFGUID         pguid,
                            LPDIRECTINPUTDEVICE* lpDEV,
                            LPDIRECTINPUTDEVICE2* lpDEV2 )
{
    HRESULT hr, hr2;
    LPDIRECTINPUTDEVICE lpdid1;
    LPDIRECTINPUTDEVICE2 lpdid2;

    hr = di->lpVtbl->CreateDevice (di, pguid, &lpdid1, NULL);

    if (SUCCEEDED (hr))
    {
        // get Device2 but only if we are not in DirectInput version 3
        if ( !bDX0300 && ( lpDEV2 != NULL ) ) {
            hr2 = lpdid1->lpVtbl->QueryInterface (lpdid1, &IID_IDirectInputDevice2, (void**)&lpdid2);
            if ( FAILED( hr2 ) ) {
                CONS_Printf ("\2Could not create IDirectInput device 2");
                lpdid2 = NULL;
            }
        }
    }
    else
        I_Error ("Could not create IDirectInput device");

    *lpDEV = lpdid1;
    if ( lpDEV2 != NULL )   //only if we requested it
        *lpDEV2 = lpdid2;
}


// ===========================================================================================
//                                                                          DIRECT INPUT MOUSE
// ===========================================================================================

#define DI_MOUSE_BUFFERSIZE             16              //number of data elements in mouse buffer

//
//  Initialise the mouse.
//
static void I_ShutdownMouse (void);
void I_StartupMouse (void)
{
    // this gets called when cv_usemouse is initted
    // for the win32 version, we want to startup the mouse later
}
// This is called just before entering the main game loop,
// when we are going fullscreen and the loading screen has finished.
void I_DoStartupMouse (void)
{
    DIPROPDWORD         dip;

    // mouse detection may be skipped by setting usemouse false
    if (cv_usemouse.value == 0 || M_CheckParm("-nomouse")) {
        mouse_enabled = false;
        return;
    }

    // acquire the mouse only once
    if (lpDIM==NULL)
    {
        CreateDevice2 (lpDI, &GUID_SysMouse, &lpDIM, NULL);

        //CONS_Printf ("I_StartupMouse()...\n");
/*
    // HEY FAB!!!!
    //        YEAH WHAT!!!
    //              TEST FOR THE MOUSE WHEEL DUMBASS!!
    //          (hmm for mouselook ?)
    //  LPDIRECTINPUTDEVICE  lpdiMouse;  // initialized previously
 
        DIDEVCAPS_DX3   DIMouseCaps; 
        HRESULT         hr; 
        BOOLEAN         HasWheel; 
 
        DIMouseCaps.dwSize = sizeof(DIMouseCaps); 
        hr = lpdiMouse->GetCapabilities(&DIMouseCaps); 
        HasWheel = ((DIMouseCaps.dwFlags & DIDC_ATTACHED) 
                && (DIMouseCaps.dwAxes > 2)); 
*/
        if (lpDIM)
        {
            if (FAILED( lpDIM->lpVtbl->SetDataFormat (lpDIM, &c_dfDIMouse) ))
                I_Error ("Couldn't set mouse data format");
        
            // create buffer for buffered data
            dip.diph.dwSize       = sizeof(dip);
            dip.diph.dwHeaderSize = sizeof(dip.diph);
            dip.diph.dwObj        = 0;
            dip.diph.dwHow        = DIPH_DEVICE;
            dip.dwData            = DI_MOUSE_BUFFERSIZE;
            if (FAILED( lpDIM->lpVtbl->SetProperty (lpDIM, DIPROP_BUFFERSIZE, &dip.diph)))
                I_Error ("Couldn't set mouse buffer size");

            if (FAILED( lpDIM->lpVtbl->SetCooperativeLevel (lpDIM, hWndMain, DISCL_EXCLUSIVE | DISCL_FOREGROUND)))
                I_Error ("Couldn't set mouse coop level");

            if (FAILED( lpDIM->lpVtbl->Acquire (lpDIM) ))
                I_Error ("Couldn't acquire mouse");
        }
        else
            I_Error ("Couldn't create mouse input");

        I_AddExitFunc (I_ShutdownMouse);
    }

    // if re-enabled while running, just set mouse_enabled true again,
    // do not acquire the mouse more than once
    mouse_enabled = true;
}


//
// Shutdown Mouse DirectInput device
//
static void I_ShutdownMouse (void)
{
    CONS_Printf ("I_ShutdownMouse()\n");
        
    if (lpDIM)
    {
        lpDIM->lpVtbl->Unacquire (lpDIM);
        lpDIM->lpVtbl->Release (lpDIM);
        lpDIM = NULL;
    }
    mouse_enabled = false;
}


//
// Get buffered data from the mouse
//
static void I_GetMouseEvents (void)
{
    DIDEVICEOBJECTDATA      rgdod[DI_MOUSE_BUFFERSIZE];
    DWORD                   dwItems;
    DWORD                   d;
    HRESULT                 hr;
    
    //DIMOUSESTATE      diMState;

    event_t         event;
    int                     xmickeys,ymickeys;

    if (!mouse_enabled)
        return;

getBufferedData:
    // may be unnecessary but never hurts
    //lpDIM->lpVtbl->Poll (lpDIM);

    dwItems = DI_MOUSE_BUFFERSIZE;
    hr = lpDIM->lpVtbl->GetDeviceData (lpDIM, sizeof(DIDEVICEOBJECTDATA),
                                       rgdod,
                                       &dwItems,
                                       0 );

    // If data stream was interrupted, reacquire the device and try again.
    if (hr==DIERR_INPUTLOST)
    {
        hr = lpDIM->lpVtbl->Acquire (lpDIM);
        if (SUCCEEDED(hr))
            goto getBufferedData;
    }

    // We can't get the device, quit.
    if (hr==DIERR_NOTACQUIRED)
    {
        I_Error ("Device not acquired! (mouse)");
    }

    // We got buffered input, act on it
    if (SUCCEEDED(hr))
    {
        xmickeys = 0;
        ymickeys = 0;

        // dwItems contains number of elements read (could be 0)
        for (d = 0; d < dwItems; d++)
        {
            if (rgdod[d].dwOfs >= DIMOFS_BUTTON0 &&
                rgdod[d].dwOfs <  DIMOFS_BUTTON0+MOUSEBUTTONS)
            {
                if (rgdod[d].dwData & 0x80)             // Button down
                    event.type = ev_keydown;
                else
                    event.type = ev_keyup;          // Button up

                event.data1 = rgdod[d].dwOfs - DIMOFS_BUTTON0 + KEY_MOUSE1;
                D_PostEvent(&event);
            }
            else if (rgdod[d].dwOfs == DIMOFS_X) {
                xmickeys += rgdod[d].dwData;
            }
            else if (rgdod[d].dwOfs == DIMOFS_Y) {
                ymickeys += rgdod[d].dwData;
            }
        }

        if (xmickeys || ymickeys)
        {
            event.type  = ev_mouse;
            event.data1 = 0;                        //woudl be buttons but not needed
            event.data2 = xmickeys;
            event.data3 = -ymickeys;
            D_PostEvent (&event);
        }
    }
}


// ===========================================================================================
//                                                                       DIRECT INPUT JOYSTICK
// ===========================================================================================

// public for game control code
    JoyType_t   Joystick;

// private
    static BYTE iJoyNum;        // used by enumeration


// ------------------
// SetDIDwordProperty ( HELPER )
// Set a DWORD property on a DirectInputDevice.
// ------------------
static HRESULT SetDIDwordProperty( LPDIRECTINPUTDEVICE pdev,
                                   REFGUID guidProperty,
                                   DWORD dwObject,
                                   DWORD dwHow,
                                   DWORD dwValue)
{
    DIPROPDWORD dipdw;

    dipdw.diph.dwSize       = sizeof(dipdw);
    dipdw.diph.dwHeaderSize = sizeof(dipdw.diph);
    dipdw.diph.dwObj        = dwObject;
    dipdw.diph.dwHow        = dwHow;
    dipdw.dwData            = dwValue;

    return pdev->lpVtbl->SetProperty( pdev, guidProperty, &dipdw.diph );
}


// ---------------
// DIEnumJoysticks
// There is no such thing as a 'system' joystick, contrary to mouse,
// we must enumerate and choose one joystick device to use
// ---------------
static BOOL CALLBACK DIEnumJoysticks ( LPCDIDEVICEINSTANCE lpddi,
                                       LPVOID pvRef )   //cv_usejoystick
{
    LPDIRECTINPUTDEVICE pdev;
    DIPROPRANGE         diprg;
    DIDEVCAPS_DX3       caps;
    BOOL                bUseThisOne = FALSE;

    iJoyNum++;

    //faB: if cv holds a string description of joystick, the value from atoi() is 0
    //     else, the value was probably set by user at console to one of the previsouly
    //     enumerated joysticks
    if ( ((consvar_t *)pvRef)->value == iJoyNum ||
         !lstrcmp( ((consvar_t *)pvRef)->string, lpddi->tszProductName ) )
        bUseThisOne = TRUE;

    //CONS_Printf (" cv joy is %s\n", ((consvar_t *)pvRef)->string);

    // print out device name
    CONS_Printf ("%c%d: %s\n",
        ( bUseThisOne ) ? '\2' : ' ',   // show name in white if this is the one we will use
        iJoyNum,
        //( GET_DIDEVICE_SUBTYPE(lpddi->dwDevType) == DIDEVTYPEJOYSTICK_GAMEPAD ) ? "Gamepad " : "Joystick",
        lpddi->tszProductName ); // , lpddi->tszInstanceName );
    
    // use specified joystick (cv_usejoystick.value in pvRef)
    if ( !bUseThisOne )
        return DIENUM_CONTINUE;

    if (lpDI->lpVtbl->CreateDevice (lpDI, &lpddi->guidInstance,
                                    &pdev, NULL) != DI_OK)
    {
        // if it failed, then we can't use this joystick for some
        // bizarre reason.  (Maybe the user unplugged it while we
        // were in the middle of enumerating it.)  So continue enumerating
        CONS_Printf ("DIEnumJoysticks(): CreateDevice FAILED\n");
        return DIENUM_CONTINUE;
    }


    // get the Device capabilities
    //
    caps.dwSize = sizeof(DIDEVCAPS_DX3);
    if ( FAILED( pdev->lpVtbl->GetCapabilities ( pdev, (DIDEVCAPS*)&caps ) ) )
    {
        CONS_Printf ("DIEnumJoysticks(): GetCapabilities FAILED\n");
        pdev->lpVtbl->Release (pdev);
        return DIENUM_CONTINUE;
    }
    if ( !(caps.dwFlags & DIDC_ATTACHED) )   // should be, since we enumerate only attached devices
        return DIENUM_CONTINUE;
    
    Joystick.bJoyNeedPoll = (( caps.dwFlags & DIDC_POLLEDDATAFORMAT ) != 0);

    if ( caps.dwFlags & DIDC_FORCEFEEDBACK )
        CONS_Printf ("Sorry, force feedback is not yet supported\n");

    Joystick.bGamepadStyle = ( GET_DIDEVICE_SUBTYPE( caps.dwDevType ) == DIDEVTYPEJOYSTICK_GAMEPAD );
    //DEBUG CONS_Printf ("Gamepad: %d\n", Joystick.bGamepadStyle);


    CONS_Printf ("Capabilities: %d axes, %d buttons, %d POVs\n",
                 caps.dwAxes, caps.dwButtons, caps.dwPOVs );
    

    // Set the data format to "simple joystick" - a predefined data format 
    //
    // A data format specifies which controls on a device we
    // are interested in, and how they should be reported.
    //
    // This tells DirectInput that we will be passing a
    // DIJOYSTATE structure to IDirectInputDevice::GetDeviceState.
    if (pdev->lpVtbl->SetDataFormat (pdev, &c_dfDIJoystick) != DI_OK)
    {
        CONS_Printf ("DIEnumJoysticks(): SetDataFormat FAILED\n");
        pdev->lpVtbl->Release (pdev);
        return DIENUM_CONTINUE;
    }

    // Set the cooperativity level to let DirectInput know how
    // this device should interact with the system and with other
    // DirectInput applications.
    if (pdev->lpVtbl->SetCooperativeLevel (pdev, hWndMain,
                        DISCL_EXCLUSIVE | DISCL_FOREGROUND) != DI_OK)
    {
        CONS_Printf ("DIEnumJoysticks(): SetCooperativeLevel FAILED\n");
        pdev->lpVtbl->Release (pdev);
        return DIENUM_CONTINUE;
    }


    // set the range of the joystick axis
    diprg.diph.dwSize       = sizeof(DIPROPRANGE);
    diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    diprg.diph.dwHow        = DIPH_BYOFFSET;
    diprg.lMin              = -JOYAXISRANGE;    // value for extreme left
    diprg.lMax              = +JOYAXISRANGE;    // value for extreme right

    diprg.diph.dwObj = DIJOFS_X;    // set the x-axis range
    if (FAILED( pdev->lpVtbl->SetProperty( pdev, DIPROP_RANGE, &diprg.diph ) ) ) {
        goto SetPropFail;
    }

    diprg.diph.dwObj = DIJOFS_Y;    // set the y-axis range
    if (FAILED( pdev->lpVtbl->SetProperty( pdev, DIPROP_RANGE, &diprg.diph ) ) ) {
SetPropFail:
        CONS_Printf ("DIEnumJoysticks(): SetProperty FAILED\n");
        pdev->lpVtbl->Release (pdev);
        return DIENUM_CONTINUE;
    }

    diprg.diph.dwObj = DIJOFS_Z;    // set the z-axis range
    if (FAILED( pdev->lpVtbl->SetProperty( pdev, DIPROP_RANGE, &diprg.diph ) ) ) {
        CONS_Printf ("DIJOFS_Z not found\n");
        // set a flag here..
    }

    diprg.diph.dwObj = DIJOFS_RZ;   // set the rudder range
    if (FAILED( pdev->lpVtbl->SetProperty( pdev, DIPROP_RANGE, &diprg.diph ) ) )
    {
        CONS_Printf ("DIJOFS_RZ (rudder) not found\n");
        // set a flag here..
    }

    // set X axis dead zone to 25% (to avoid accidental turning)
    if ( !Joystick.bGamepadStyle ) {
        if ( FAILED( SetDIDwordProperty (pdev, DIPROP_DEADZONE, DIJOFS_X,
                                         DIPH_BYOFFSET, 2500) ) )
        {
            CONS_Printf ("DIEnumJoysticks(): couldn't SetProperty for DEAD ZONE\n");
            //pdev->lpVtbl->Release (pdev);
            //return DIENUM_CONTINUE;
        }
        if (FAILED( SetDIDwordProperty (pdev, DIPROP_DEADZONE, DIJOFS_Y,
                                        DIPH_BYOFFSET, 2500) ) )
        {
            CONS_Printf ("DIEnumJoysticks(): couldn't SetProperty for DEAD ZONE\n");
            //pdev->lpVtbl->Release (pdev);
            //return DIENUM_CONTINUE;
        }
    }

    // query for IDirectInputDevice2 - we need this to poll the joystick 
    if ( bDX0300 ) {
        // we won't use the poll
        lpDIJ2 = NULL;
    }
    else
    {
        if (FAILED( pdev->lpVtbl->QueryInterface(pdev, &IID_IDirectInputDevice2,
                                                 (LPVOID *)&lpDIJ2) ) )
        {
            CONS_Printf ("DIEnumJoysticks(): QueryInterface FAILED\n");
            pdev->lpVtbl->Release (pdev);
            return DIENUM_CONTINUE;
        }
    }
    
    // we successfully created an IDirectInputDevice.  So stop looking 
    // for another one.
    lpDIJ = pdev;
    return DIENUM_STOP;
}


// --------------
// I_InitJoystick
// This is called everytime the 'use_joystick' variable changes
// It is normally called at least once at startup when the config is loaded
// --------------
static void I_ShutdownJoystick (void);
void I_InitJoystick (void)
{
    HRESULT hr;

    // cleanup
    I_ShutdownJoystick ();
    
    joystick_detected = false;

    // joystick detection can be skipped by setting use_joystick to 0
    if ( M_CheckParm("-nojoy") ) {
        CONS_Printf ("Joystick disabled\n");
        return;
    }
    else
        // don't do anything at the registration of the joystick cvar,
        // until config is loaded
        if ( !lstrcmp( cv_usejoystick.string, "0" ) )
            return;

    // acquire the joystick only once
    if (lpDIJ==NULL)
    {
        joystick_detected = false;

        CONS_Printf ("Looking for joystick devices:\n");
        iJoyNum = 0;
        hr = lpDI->lpVtbl->EnumDevices( lpDI, DIDEVTYPE_JOYSTICK, 
                                        DIEnumJoysticks,
                                        (void*)&cv_usejoystick,    // our user parameter is joystick number
                                        DIEDFL_ATTACHEDONLY );
        if (FAILED(hr)) {
            CONS_Printf ("\nI_InitJoystick(): EnumDevices FAILED\n");
            return;
        }

        if (lpDIJ == NULL)
        {
            if (iJoyNum == 0)
                CONS_Printf ("none found\n");
            else
            {
                CONS_Printf ("none used\n");
                if ( cv_usejoystick.value > 0 &&
                     cv_usejoystick.value > iJoyNum )
                {
                    CONS_Printf ("\2Set the use_joystick variable to one of the"
                                 " enumerated joysticks number\n");
                }
            }
            return;
        }

        I_AddExitFunc (I_ShutdownJoystick);

        // set coop level
        if ( FAILED( lpDIJ->lpVtbl->SetCooperativeLevel (lpDIJ, hWndMain, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND) ))
            I_Error ("I_InitJoystick: SetCooperativeLevel FAILED");

        if ( FAILED( lpDIJ->lpVtbl->Acquire (lpDIJ) ))
            I_Error ("Couldn't acquire Joystick");

        joystick_detected = true;
    }
    else
        CONS_Printf ("Joystick already initialized\n");

    //faB: we don't unacquire joystick, so let's just pretend we re-acquired it
    joystick_detected = true;
}


static void I_ShutdownJoystick (void)
{
    if ( joystick_detected )
        CONS_Printf ("I_ShutdownJoystick()\n");
        
    if (lpDIJ)
    {
        lpDIJ->lpVtbl->Unacquire (lpDIJ);
        lpDIJ->lpVtbl->Release (lpDIJ);
        lpDIJ = NULL;
    }
    if (lpDIJ2)
    {
        lpDIJ2->lpVtbl->Release(lpDIJ2);
        lpDIJ2 = NULL;
    }
    joystick_detected = false;
}


// -------------------
// I_GetJoystickEvents
// Get current joystick axis and button states
// -------------------
static void I_GetJoystickEvents (void)
{
    HRESULT     hr;
    DIJOYSTATE  js;          // DirectInput joystick state 
    int         i;
    static DWORD lastjoybuttons = 0;
    DWORD  joybuttons;
    event_t event;

    if (lpDIJ==NULL)
        return;

    hr = DIERR_INPUTLOST;

    // if input is lost then acquire and keep trying 
    while( hr == DIERR_INPUTLOST ) 
    {
        // poll the joystick to read the current state
        //faB: if the device doesn't require polling, this function returns
        //     almost instantly
        if ( lpDIJ2 ) {
            hr = lpDIJ2->lpVtbl->Poll(lpDIJ2);
            if ( FAILED(hr) ) {
                CONS_Printf ("I_GetJoystickEvents(): Poll FAILED\n");
                return;
            }
        }

        // get the input's device state, and put the state in dims
        hr = lpDIJ->lpVtbl->GetDeviceState( lpDIJ, sizeof(DIJOYSTATE), &js );

        if ( hr == DIERR_INPUTLOST )
        {
            // DirectInput is telling us that the input stream has
            // been interrupted.  We aren't tracking any state
            // between polls, so we don't have any special reset
            // that needs to be done.  We just re-acquire and
            // try again.
            hr = lpDIJ->lpVtbl->Acquire( lpDIJ );
            if ( FAILED(hr) ) {
                CONS_Printf ("I_GetJoystickEvents(): Acquire FAILED\n");
                return;
            }
        }
    }

    if ( FAILED( hr ) )   {
        CONS_Printf ("I_GetJoystickEvents(): GetDeviceState FAILED\n");
        return;
    }
        
    // post virtual key events for buttons
    //

    joybuttons = 0;
    //faB: look for as much buttons as g_input code supports,
    //     we don't use the others
    for (i = JOYBUTTONS-1; i >= 0; i--)
    {
        joybuttons <<= 1;
        if ( js.rgbButtons[i] )
            joybuttons |= 1;
    }

    if ( joybuttons != lastjoybuttons )
    {
        DWORD   j = 1;
        DWORD   newbuttons;

        // keep only bits that changed since last time
        newbuttons = joybuttons ^ lastjoybuttons;    
        lastjoybuttons = joybuttons;

        for( i=0; i < JOYBUTTONS; i++, j<<=1 )
        {
            if ( newbuttons & j )      // button changed state ?
            {
                if ( joybuttons & j )
                    event.type = ev_keydown;
                else
                    event.type = ev_keyup;
                event.data1 = KEY_JOY1 + i;
                D_PostEvent (&event);
            }
        }
    }

    
    // send joystick axis positions
    //
    event.type = ev_joystick;
    event.data1 = 0;
    event.data2 = 0;
    event.data3 = 0;

    if ( Joystick.bGamepadStyle )
    {
        // gamepad control type, on or off, live or die
        if ( js.lX < -(JOYAXISRANGE/2) )
            event.data2 = -1;
        else if ( js.lX > (JOYAXISRANGE/2) )
            event.data2 = 1;
        if ( js.lY < -(JOYAXISRANGE/2) )
            event.data3 = -1;
        else if ( js.lY > (JOYAXISRANGE/2) )
            event.data3 = 1;
    }
    else
    {
        // analog control style , just send the raw data
        event.data2 = js.lX;    // x axis
        event.data3 = js.lY;    // y axis
    }

    D_PostEvent(&event);
}
    
    
// ===========================================================================================
//                                                                       DIRECT INPUT KEYBOARD
// ===========================================================================================

byte ASCIINames[256] = {
        //  0       1       2       3       4       5       6       7
        //  8       9       A       B       C       D       E       F
    0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6',
        '7',    '8',    '9',    '0', KEY_MINUS,KEY_EQUALS,KEY_BACKSPACE, KEY_TAB,
        'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
        'o',    'p',    '[',    ']', KEY_ENTER,KEY_CTRL,'a',    's',
        'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
        '\'',   '`', KEY_SHIFT, '\\',   'z',    'x',    'c',    'v',
        'b',    'n',    'm',    ',',    '.',    '/', KEY_SHIFT, '*',
        KEY_ALT,KEY_SPACE,KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
        KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,KEY_NUMLOCK,KEY_SCROLLLOCK,KEY_KEYPAD7,
        KEY_KEYPAD8,KEY_KEYPAD9,KEY_MINUSPAD,KEY_KEYPAD4,KEY_KEYPAD5,KEY_KEYPAD6,KEY_PLUSPAD,KEY_KEYPAD1,
        KEY_KEYPAD2,KEY_KEYPAD3,KEY_KEYPAD0,KEY_KPADDEL,      0,      0,      0,      KEY_F11,
        KEY_F12,0,          0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,

        //  0       1       2       3       4       5       6       7
        //  8       9       A       B       C       D       E       F

        0,          0,      0,      0,      0,      0,      0,      0,          //0x80
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,  KEY_ENTER,KEY_CTRL, 0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,          //0xa0
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0, KEY_KPADDEL, 0,KEY_KPADSLASH,0,      0,
        KEY_ALT,0,          0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,          KEY_HOME,               //0xc0
        KEY_UPARROW,KEY_PGUP,0,KEY_LEFTARROW,0,KEY_RIGHTARROW,0,KEY_END,
        KEY_DOWNARROW,KEY_PGDN, KEY_INS,KEY_DEL,0,0,0,0,
        0,          0,      0,KEY_LEFTWIN,KEY_RIGHTWIN,KEY_MENU, 0, 0,
        0,          0,      0,      0,      0,      0,      0,      0,          //0xe0
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0,
        0,          0,      0,      0,      0,      0,      0,      0
};

int pausepressed=0;

//  Return a key that has been pushed, or 0
//  (replace getchar() at game startup)
//
int I_GetKey (void)
{
    event_t   *ev;
        
    if (eventtail != eventhead)
    {
        ev = &events[eventtail];
        eventtail = (++eventtail)&(MAXEVENTS-1);
        if (ev->type == ev_keydown)
            return ev->data1;
        else
            return 0;
    }
    return 0;
}


// -----------------
// I_StartupKeyboard
// Installs DirectInput keyboard
// -----------------
#define DI_KEYBOARD_BUFFERSIZE          32              //number of data elements in keyboard buffer
void I_StartupKeyboard()
{
    DIPROPDWORD dip;

    //faB: detect error
    if (lpDIK != NULL) {
        CONS_Printf ("\2I_StartupKeyboard(): called twice\n");
        return;
    }

    CreateDevice2 (lpDI, &GUID_SysKeyboard, &lpDIK, NULL);

    if (lpDIK)
    {
        if (FAILED( lpDIK->lpVtbl->SetDataFormat (lpDIK, &c_dfDIKeyboard) ))
            I_Error ("Couldn't set keyboard data format");
            
        // create buffer for buffered data
        dip.diph.dwSize       = sizeof(dip);
        dip.diph.dwHeaderSize = sizeof(dip.diph);
        dip.diph.dwObj        = 0;
        dip.diph.dwHow        = DIPH_DEVICE;
        dip.dwData            = DI_KEYBOARD_BUFFERSIZE;
        if (FAILED( lpDIK->lpVtbl->SetProperty (lpDIK, DIPROP_BUFFERSIZE, &dip.diph)))
            I_Error ("Couldn't set keyboard buffer size");
    
        if (FAILED( lpDIK->lpVtbl->SetCooperativeLevel (lpDIK, hWndMain, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND)))
            I_Error ("Couldn't set keyboard coop level");

        //faB: it seems to FAIL if the window doesn't have the focus
        if (FAILED( lpDIK->lpVtbl->Acquire (lpDIK) ))
            I_Error ("Couldn't acquire keyboard\n");
    }
    else
        I_Error ("Couldn't create keyboard input");

    I_AddExitFunc (I_ShutdownKeyboard);
    hacktics = 0;                       //faB: see definition
    keyboard_started = true;
}


// ------------------
// I_ShutdownKeyboard
// Release DirectInput keyboard.
// ------------------
static void I_ShutdownKeyboard()
{
    if (!keyboard_started)
        return;
        
    CONS_Printf ("I_ShutdownKeyboard()\n");
        
    if ( lpDIK )
    {
        lpDIK->lpVtbl->Unacquire (lpDIK);
        lpDIK->lpVtbl->Release (lpDIK);
        lpDIK = NULL;
    }

    keyboard_started = false;
}


// -------------------
// I_GetKeyboardEvents
// Get buffered data from the keyboard
// -------------------
static void I_GetKeyboardEvents (void)
{
    static  boolean         KeyboardLost = false;
    
    //faB: simply repeat the last pushed key every xx tics,
    //     make more user friendly input for Console and game Menus
    #define KEY_REPEAT_DELAY    2      // TICRATE tics, repeat every 1/3 second
    static  long            RepeatKeyTics = 0;
    static  BYTE            RepeatKeyCode;
    
    DIDEVICEOBJECTDATA      rgdod[DI_KEYBOARD_BUFFERSIZE];
    DWORD                   dwItems;
    DWORD                   d;
    HRESULT                 hr;
    int                     ch;
    
    event_t         event;
    
    if (!keyboard_started)
        return;
    
getBufferedData:
    dwItems = DI_KEYBOARD_BUFFERSIZE;
    hr = lpDIK->lpVtbl->GetDeviceData (lpDIK, sizeof(DIDEVICEOBJECTDATA),
                                       rgdod,
                                       &dwItems,
                                       0 );
    
    // If data stream was interrupted, reacquire the device and try again.
    if (hr == DIERR_INPUTLOST)
    {
        // why it succeeds to acquire just after I don't understand.. so I set the flag BEFORE
        KeyboardLost = true;
        
        hr = lpDIK->lpVtbl->Acquire (lpDIK);
        if (SUCCEEDED(hr))
            goto getBufferedData;
        
        // app is in background probably, we can't get keyboard events
        CONS_Printf ("we lost the input!");
    }
    
    // We can't get the device, quit.
    if (hr==DIERR_NOTACQUIRED)
    {
        I_Error ("Device not acquired! (keyboard)");
    }
    
    // we lost data, get device actual state to recover lost information
    if (hr==DI_BUFFEROVERFLOW)
    {
        I_Error ("DI buffer overflow (keyboard)");
        //I_RecoverKeyboardState ();
        
        //hr = lpDIM->lpVtbl->GetDeviceState (lpDIM, sizeof(keys), &diMouseState);
    }
    
    // We got buffered input, act on it
    if (SUCCEEDED(hr))
    {
        // if we previously lost keyboard data, recover its current state
        if (KeyboardLost)
        {
            //faB: my current hack simply clear the keys so we don't have the last pressed keys
            // still active.. to have to re-trigger it is not much trouble for the user.
            memset (gamekeydown, 0, sizeof(gamekeydown));
            //CONS_Printf ("we lost it, we cleared stuff!\n");
            KeyboardLost = false;
        }
        
        // dwItems contains number of elements read (could be 0)
        for (d = 0; d < dwItems; d++)
        {
            // dwOfs member is DIK_* value
            // dwData member 0x80 bit set press down, clear is release
            
            if (rgdod[d].dwData & 0x80)
                event.type = ev_keydown;
            else
                event.type = ev_keyup;
            
            ch = rgdod[d].dwOfs & 0xFF;
            if (ASCIINames[ch]!=0)
                event.data1 = ASCIINames[ch];
            else
                event.data1 = ch + 0x80;
            
            D_PostEvent(&event);
        }

        //
        // Key Repeat
        //
        if (dwItems) {
            // new key events, so stop repeating key
            RepeatKeyCode = 0;
            RepeatKeyTics = hacktics + (KEY_REPEAT_DELAY*2);   //delay is trippled for first repeating key
            if (event.type == ev_keydown)       // use the last event!
                RepeatKeyCode = event.data1;
        }
        else {
            // no new keys, repeat last pushed key after some time
            if (RepeatKeyCode &&
                (hacktics - RepeatKeyTics) > KEY_REPEAT_DELAY)
            {
                event.type = ev_keydown;
                event.data1 = RepeatKeyCode;
                D_PostEvent (&event);

                RepeatKeyTics = hacktics;
            }
        }
    }
}


//
// Closes DirectInput
//
static void I_ShutdownDirectInput (void)
{
    if (lpDI!=NULL)
        lpDI->lpVtbl->Release (lpDI);
    lpDI = NULL;
}


//  This stuff should get rid of the exception and page faults when
//  Doom bugs out with an error. Now it should exit cleanly.
//
int  I_StartupSystem(void)
{
    HRESULT hr;

    // some 'more globals than globals' things to initialize here ?
    graphics_started = false;
    keyboard_started = false;
    sound_started = false;
    timer_started = false;
    cdaudio_started = false;
        
    // check for OS type and version here ?
#ifdef NDEBUG
    signal(SIGABRT, signal_handler);
    signal(SIGFPE , signal_handler);
    signal(SIGILL , signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT , signal_handler);
#endif

    // create DirectInput - so that I_StartupKeyboard/Mouse can be called later on
    // from D_DoomMain just like DOS version
    hr = DirectInputCreate (myInstance, DIRECTINPUT_VERSION, &lpDI, NULL);

    if ( SUCCEEDED( hr ) )
        bDX0300 = FALSE;
    else
    {
        // try opening DirectX3 interface for NT compatibility
        hr = DirectInputCreate (myInstance, DXVERSION_NTCOMPATIBLE, &lpDI, NULL);

        if ( FAILED ( hr ) )
        {
            char* sErr;
            switch(hr)
            {
            case DIERR_BETADIRECTINPUTVERSION:
                sErr = "DIERR_BETADIRECTINPUTVERSION";
                break;
            case DIERR_INVALIDPARAM :
                sErr = "DIERR_INVALIDPARAM";
                break;
            case DIERR_OLDDIRECTINPUTVERSION :
                sErr = "DIERR_OLDDIRECTINPUTVERSION";
                break;
            case DIERR_OUTOFMEMORY :
                sErr = "DIERR_OUTOFMEMORY";
                break;
            default:
                sErr = "UNKNOWN";
                break;
            }
            I_Error ("Couldn't create DirectInput (reason: %s)", sErr);
        }
        else
            CONS_Printf ("\2Using DirectX3 interface\n");

        // only use DirectInput3 compatible structures and calls
        bDX0300 = TRUE;
    }
    I_AddExitFunc (I_ShutdownDirectInput);
    return 0;
}


//  Closes down everything. This includes restoring the initial
//  pallete and video mode, and removing whatever mouse, keyboard, and
//  timer routines have been installed.
//
//  NOTE : Shutdown user funcs. are effectively called in reverse order.
//
void I_ShutdownSystem()
{
    int c;
    
    for (c=MAX_QUIT_FUNCS-1; c>=0; c--)
    {
        if (quit_funcs[c])
            (*quit_funcs[c])();
    }
}


// ---------------
// I_SaveMemToFile
// Save as much as iLength bytes starting at pData, to
// a new file of given name. The file is overwritten if it is present.
// ---------------
void I_SaveMemToFile (unsigned char* pData, unsigned long iLength, char* sFileName)
{
    HANDLE  fileHandle;
    DWORD   bytesWritten;
    fileHandle = CreateFile (sFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        I_Error ("SaveMemToFile");
    }
    WriteFile (fileHandle, pData, iLength, &bytesWritten, NULL);
    CloseHandle (fileHandle);
}
