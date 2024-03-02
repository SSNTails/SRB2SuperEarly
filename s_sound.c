// s_sound.c :

#include "doomdef.h"
#include "command.h"
#include "g_game.h"
#include "m_argv.h"
#include "r_main.h"     //R_PointToAngle2() used to calc stereo sep.
#include "r_things.h"     // for skins

#include "i_sound.h"
#include "s_sound.h"
#include "w_wad.h"
#include "z_zone.h"

// stereo reverse 1=true, 0=false
consvar_t stereoreverse = {"stereoreverse","0",CV_SAVE ,CV_OnOff};

// if true, all sounds are loaded at game startup
consvar_t precachesound = {"precachesound","0",CV_SAVE ,CV_OnOff};

CV_PossibleValue_t soundvolume_cons_t[]={{0,"MIN"},{31,"MAX"},{0,NULL}};
// actual general (maximum) sound & music volume, saved into the config
consvar_t cv_soundvolume = {"soundvolume","15",CV_SAVE,soundvolume_cons_t};
consvar_t cv_musicvolume = {"musicvolume","15",CV_SAVE,soundvolume_cons_t};

// number of channels available
void SetChannelsNum(void);
consvar_t cv_numChannels = {"snd_channels","16",CV_SAVE | CV_CALL, CV_Unsigned,SetChannelsNum};

#define S_MAX_VOLUME            127

// when to clip out sounds
// Does not fit the large outdoor areas.
// added 2-2-98 in 8 bit volume control (befort  (1200*0x10000))
#define S_CLIPPING_DIST         (1200*0x10000)

// Distance tp origin when sounds should be maxed out.
// This should relate to movement clipping resolution
// (see BLOCKMAP handling).
// Originally: (200*0x10000).
// added 2-2-98 in 8 bit volume control (befort  (160*0x10000))
#define S_CLOSE_DIST            (160*0x10000)

// added 2-2-98 in 8 bit volume control (befort  remove the +4)
#define S_ATTENUATOR            ((S_CLIPPING_DIST-S_CLOSE_DIST)>>(FRACBITS+4))

// Adjustable by menu.
#define NORM_VOLUME             snd_MaxVolume

#define NORM_PITCH              128
#define NORM_PRIORITY           64
#define NORM_SEP                128

#define S_PITCH_PERTURB         1
#define S_STEREO_SWING          (96*0x10000)

// percent attenuation from front to back
#define S_IFRACVOL              30

typedef struct
{
    // sound information (if null, channel avail.)
    sfxinfo_t*  sfxinfo;

    // origin of sound
    void*       origin;

    // handle of the sound being played
    int         handle;

} channel_t;


// the set of channels available
static channel_t*       channels;


// whether songs are mus_paused
static boolean          mus_paused;

// music currently being played
static musicinfo_t*     mus_playing=0;

static int              nextcleanup;


//
// Internals.
//
int
S_getChannel
( void*         origin,
  sfxinfo_t*    sfxinfo );


int
S_AdjustSoundParams
( mobj_t*       listener,
  mobj_t*       source,
  int*          vol,
  int*          sep,
  int*          pitch );

void S_StopChannel(int cnum);


void S_RegisterSoundStuff (void)
{
    //added:11-04-98: stereoreverse
    CV_RegisterVar (&stereoreverse);
    CV_RegisterVar (&precachesound);
}

void SetChannelsNum(void)
{
    int i;

    // Allocating the internal channels for mixing
    // (the maximum number of sounds rendered
    // simultaneously) within zone memory.
    if(channels)
      Z_Free(channels);

    channels =
      (channel_t *) Z_Malloc(cv_numChannels.value*sizeof(channel_t), PU_STATIC, 0);

    // Free all channels for use
    for (i=0 ; i<cv_numChannels.value ; i++)
      channels[i].sfxinfo = 0;

    
}

//
// Initializes sound stuff, including volume
// Sets channels, SFX and music volume,
//  allocates channel buffer, sets S_sfx lookup.
//
void S_Init ( int           sfxVolume,
              int           musicVolume )
{
    int           i;

    //CONS_Printf( "S_Init: default sfx volume %d\n", sfxVolume);

    S_SetSfxVolume (sfxVolume);
    S_SetMusicVolume (musicVolume);

    SetChannelsNum();

    // no sounds are playing, and they are not mus_paused
    mus_paused = 0;

    // Note that sounds have not been cached (yet).
    for (i=1 ; i<NUMSFX ; i++)
        S_sfx[i].lumpnum = S_sfx[i].usefulness = -1;      // for I_GetSfx()

    //
    //  precache sounds if requested by cmdline, or precachesound var true
    //
    if(!nosound && (M_CheckParm("-precachesound") || precachesound.value) )
    {
        // Initialize external data (all sounds) at start, keep static.
        CONS_Printf("Loading sounds... ");

        for (i=1 ; i<NUMSFX ; i++)
        {
            // NOTE: linked sounds use the link's data at StartSound time
            if (S_sfx[i].name && !S_sfx[i].link)
                S_sfx[i].data = I_GetSfx (&S_sfx[i]);
        }

        CONS_Printf(" pre-cached all sound data\n");
    }

}

//  Retrieve the lump number of sfx
//
int S_GetSfxLumpNum (sfxinfo_t* sfx)
{
    char namebuf[9];
    int  sfxlump;

    sprintf(namebuf, "ds%s", sfx->name);

//  CONS_Printf("%s ",sfxname);
    if ((sfxlump=W_CheckNumForName(namebuf)) == -1)
    {
        //CONS_Printf("loaded default sound\n");
        return W_GetNumForName ("dspistol");
    }

    return sfxlump;
}


//
// Per level startup code.
// Kills playing sounds at start of level,
//  determines music if any, changes music.
//
void S_Start(void)
{
  int cnum;
  int mnum;


  // kill all playing sounds at start of level
  //  (trust me - a good idea)
  for (cnum=0 ; cnum<cv_numChannels.value ; cnum++)
    if (channels[cnum].sfxinfo)
      S_StopChannel(cnum);

  // start new music for the level
  mus_paused = 0;

  if (gamemode == commercial)
    mnum = mus_runnin + gamemap - 1;
  else
  {
    int spmus[]=
    {
      // Song - Who? - Where?

      mus_e3m4, // American     e4m1
      mus_e3m2, // Romero       e4m2
      mus_e3m3, // Shawn        e4m3
      mus_e1m5, // American     e4m4
      mus_e2m7, // Tim  e4m5
      mus_e2m4, // Romero       e4m6
      mus_e2m6, // J.Anderson   e4m7 CHIRON.WAD
      mus_e2m5, // Shawn        e4m8
      mus_e1m9  // Tim          e4m9
    };

    if (gameepisode < 4)
        mnum = mus_e1m1 + (gameepisode-1)*9 + gamemap-1;
    else
        mnum = spmus[gamemap-1];
    }

  // HACK FOR COMMERCIAL
  //  if (commercial && mnum > mus_e3m9)
  //      mnum -= mus_e3m9;

  S_ChangeMusic(mnum, true);
  nextcleanup = 15;
}

extern boolean nosound;

void S_StartSoundAtVolume( void*         origin_p,
                           int           sfx_id,
                           int           volume )
{

    int           sep;
    int           pitch;
    int           priority;
    sfxinfo_t*    sfx;
    int           cnum;

    mobj_t*       origin = (mobj_t *) origin_p;

    if(nosound || (origin && origin->type == MT_SPIRIT))
        return;

    // Debug.
    /*fprintf( stderr,
             "S_StartSoundAtVolume: playing sound %d (%s)\n",
             sfx_id, S_sfx[sfx_id].name );*/

#ifdef PARANOIA
    // check for bogus sound #
    if (sfx_id < 1 || sfx_id > NUMSFX)
        I_Error("Bad sfx #: %d\n", sfx_id);
#endif

    sfx = &S_sfx[sfx_id];

    if (sfx->skinsound!=-1 && origin && origin->skin)
    {
        // seriously, it redirect player sound to the sound in the skin table
        sfx_id = ((skin_t *)origin->skin)->soundsid[sfx->skinsound];
        sfx    = &S_sfx[sfx_id];
    }

    // Initialize sound parameters
    if (sfx->link)
    {
      pitch = sfx->pitch;
      priority = sfx->priority;
      volume += sfx->volume;

      if (volume < 1)
        return;

    // added 2-2-98 SfxVolume is now the hardware volume, don't mix up
    //    if (volume > SfxVolume)
    //      volume = SfxVolume;
    }
    else
    {
      pitch = NORM_PITCH;
      priority = NORM_PRIORITY;
    }


    // Check to see if it is audible,
    //  and if not, modify the params

    //added:16-01-98:changed consoleplayer to displayplayer
    if (origin && origin != players[displayplayer].mo && !(cv_splitscreen.value && origin == players[secondarydisplayplayer].mo))
    {
        int           rc,rc2;
        int volume2=volume,sep2=sep,pitch2=pitch;
        rc=S_AdjustSoundParams(players[displayplayer].mo,
                               origin,
                               &volume,
                               &sep,
                               &pitch);
        if(cv_splitscreen.value)
        {
             rc2=S_AdjustSoundParams(players[secondarydisplayplayer].mo,
                                     origin,
                                     &volume2,
                                     &sep2,
                                     &pitch2);
             if(!rc2)
             {
                 if( !rc )
                     return;
             }
             else
             if(!rc || (rc && volume2>volume))
             {
                 volume=volume2;
                 sep=sep2;
                 pitch=pitch2;
                 if ( origin->x == players[secondarydisplayplayer].mo->x &&
                      origin->y == players[secondarydisplayplayer].mo->y )
                 {
                     sep       = NORM_SEP;
                 }
             }
        }
        else
            if(!rc) return;

        if ( origin->x == players[displayplayer].mo->x &&
             origin->y == players[displayplayer].mo->y )
      {
        sep       = NORM_SEP;
      }
    }
    else
    {
      sep = NORM_SEP;
    }

    // hacks to vary the sfx pitches

    //added:16-02-98: removed by Fab, because it used M_Random() and it
    //                was a big bug, and then it doesnt change anything
    //                dont hear any diff. maybe I'll put it back later
    //                but of course not using M_Random().

    // kill old sound
    S_StopSound(origin);

    // try to find a channel
    cnum = S_getChannel(origin, sfx);

    if (cnum<0)
      return;

    //
    // This is supposed to handle the loading/caching.
    // For some odd reason, the caching is done nearly
    //  each time the sound is needed?
    //

    // cache data if necessary
    // NOTE : set sfx->data NULL sfx->lump -1 to force a reload
    if (sfx->link)
        sfx->data = sfx->link->data;

    if (!sfx->data)
    {
        //CONS_Printf ("cached sound %s\n", sfx->name);
        if (!sfx->link)
            sfx->data = I_GetSfx (sfx);
        else
        {
            sfx->data = I_GetSfx (sfx->link);
            sfx->link->data = sfx->data;
        }
    }

    // increase the usefulness
    if (sfx->usefulness++ < 0)
        sfx->usefulness = -1;

    //added:11-04-98:
    if (stereoreverse.value)
        sep = (~sep) & 255;

    //CONS_Printf("stereo %d reverse %d\n", sep, stereoreverse.value);

    // Assigns the handle to one of the channels in the
    //  mix/output buffer.
    channels[cnum].handle = I_StartSound(sfx_id,
                                         /*sfx->data,*/
                                         volume,
                                         sep,
                                         pitch,
                                         priority);
}

void S_StartSound( void*         origin,
                   int           sfx_id )
{
    // the volume is handled 8 bits
    S_StartSoundAtVolume(origin, sfx_id, 255);
}


void S_StopSound(void *origin)
{
    int cnum;

    for (cnum=0 ; cnum<cv_numChannels.value ; cnum++)
    {
        if (channels[cnum].sfxinfo && channels[cnum].origin == origin)
        {
            S_StopChannel(cnum);
            break;
        }
    }
}



//
// Stop and resume music, during game PAUSE.
//
void S_PauseSound(void)
{
    if (mus_playing && !mus_paused)
    {
        I_PauseSong(mus_playing->handle);
        mus_paused = true;
    }

    // pause cd music
    I_StopCD ();
}

void S_ResumeSound(void)
{
    if (mus_playing && mus_paused)
    {
        I_ResumeSong(mus_playing->handle);
        mus_paused = false;
    }

    // resume cd music
    I_ResumeCD ();
}


//
// Updates music & sounds
//
static int      actualsfxvolume;        //check for change through console
static int      actualmusicvolume;

void S_UpdateSounds(void)
{
    int         audible;
    int         cnum;
    int         volume;
    int         sep;
    int         pitch;
    sfxinfo_t*  sfx;
    channel_t*  c;

    mobj_t*     listener = players[displayplayer].mo;

    // Update sound/music volumes, if changed manually at console
    if (actualsfxvolume != cv_soundvolume.value)
        S_SetSfxVolume (cv_soundvolume.value);
    if (actualmusicvolume != cv_musicvolume.value)
        S_SetMusicVolume (cv_musicvolume.value);


    /* Clean up unused data.
    if (gametic > nextcleanup)
    {
        for (i=1 ; i<NUMSFX ; i++)
        {
            if (S_sfx[i].usefulness==0)
            {
                //S_sfx[i].usefulness--;

                // don't forget to unlock it !!!
                // __dmpi_unlock_....
                //Z_ChangeTag(S_sfx[i].data, PU_CACHE);
                //S_sfx[i].data = 0;

                CONS_Printf ("\2flushed sfx %.6s\n", S_sfx[i].name);
            }
        }
        nextcleanup = gametic + 15;
    }*/

    for (cnum=0 ; cnum<cv_numChannels.value ; cnum++)
    {
        c = &channels[cnum];
        sfx = c->sfxinfo;

        if (c->sfxinfo)
        {
            if (I_SoundIsPlaying(c->handle))
            {
                // initialize parameters
                volume = 255;            //8 bits internal volume precision
                pitch = NORM_PITCH;
                sep = NORM_SEP;

                if (sfx->link) // strange (BP)
                {
                    pitch = sfx->pitch;
                    volume += sfx->volume;
                    if (volume < 1)
                    {
                        S_StopChannel(cnum);
                        continue;
                    }
                }

                // check non-local sounds for distance clipping
                //  or modify their params
                if (c->origin && listener != c->origin && !(cv_splitscreen.value && c->origin==players[secondarydisplayplayer].mo))
                {
                    int audible2;
                    int volume2=volume,sep2=sep,pitch2=pitch;
                    audible = S_AdjustSoundParams(listener,
                                                  c->origin,
                                                  &volume,
                                                  &sep,
                                                  &pitch);

                    if(cv_splitscreen.value)
                    {
                         audible2=S_AdjustSoundParams(players[secondarydisplayplayer].mo,
                                                      c->origin,
                                                      &volume2,
                                                      &sep2,
                                                      &pitch2);
                         if(audible2 && (!audible || (audible && volume2>volume)))
                         {
                             audible=true;
                             volume=volume2;
                             sep=sep2;
                             pitch=pitch2;
                         }
                    }
            
                    if (!audible)
                    {
                        S_StopChannel(cnum);
                    }
                    else
                        I_UpdateSoundParams(c->handle, volume, sep, pitch);
                }
            }
            else
            {
                // if channel is allocated but sound has stopped,
                //  free it
                S_StopChannel(cnum);
            }
        }
    }
    // kill music if it is a single-play && finished
    // if (     mus_playing
    //      && !I_QrySongPlaying(mus_playing->handle)
    //      && !mus_paused )
    // S_StopMusic();
}


void S_SetMusicVolume(int volume)
{
    if (volume < 0 || volume > 31)
        CONS_Printf("musicvolume should be between 0-31\n");

    CV_SetValue (&cv_musicvolume, volume&31);
    actualmusicvolume = cv_musicvolume.value;   //check for change of var

#ifndef __WIN32__
    I_SetMusicVolume(31);       //faB: this is a trick for buggy dos drivers.. I think.
                                //     win32 midistream playbakc doesn't need this
#endif
    I_SetMusicVolume(volume&31);
}



void S_SetSfxVolume(int volume)
{
    if (volume < 0 || volume > 31)
        CONS_Printf("sfxvolume should be between 0-31\n");

    CV_SetValue (&cv_soundvolume, volume&31);
    actualsfxvolume = cv_soundvolume.value;       //check for change of var

    // now hardware volume
    I_SetSfxVolume (volume&31);
}

//
// Starts some music with the music id found in sounds.h.
//
void S_StartMusic(int m_id)
{
    S_ChangeMusic(m_id, false);
}

void S_ChangeMusic( int                   musicnum,
                    int                   looping )
{
    musicinfo_t*        music;

    if ( (musicnum <= mus_None) ||
         (musicnum >= NUMMUSIC) )
    {
        CONS_Printf ("ERROR: Bad music number %d\n", musicnum);
        return;
    }
    else
        music = &S_music[musicnum];

    if (mus_playing == music)
        return;

    // shutdown old music
    S_StopMusic();

    // get lumpnum if neccessary
    if (!music->lumpnum)
         music->lumpnum = W_GetNumForName( va("d_%s", music->name));

    // load & register it
    music->data = (void *) W_CacheLumpNum(music->lumpnum, PU_MUSIC);
    music->handle = I_RegisterSong(music->data,W_LumpLength(music->lumpnum));

    // play it
    I_PlaySong(music->handle, looping);

    mus_playing = music;
}


void S_StopMusic(void)
{
    if (mus_playing)
    {
        if (mus_paused)
            I_ResumeSong(mus_playing->handle);

        I_StopSong(mus_playing->handle);
        I_UnRegisterSong(mus_playing->handle);
        Z_ChangeTag(mus_playing->data, PU_CACHE);

        mus_playing->data = 0;
        mus_playing = 0;
    }
}




void S_StopChannel(int cnum)
{

    int         i;
    channel_t*  c = &channels[cnum];

    if (c->sfxinfo)
    {
        // stop the sound playing
        if (I_SoundIsPlaying(c->handle))
        {
            I_StopSound(c->handle);
        }

        // check to see
        //  if other channels are playing the sound
        for (i=0 ; i<cv_numChannels.value ; i++)
        {
            if (cnum != i
                && c->sfxinfo == channels[i].sfxinfo)
            {
                break;
            }
        }

        // degrade usefulness of sound data
        c->sfxinfo->usefulness--;

        c->sfxinfo = 0;
    }
}



//
// Changes volume, stereo-separation, and pitch variables
//  from the norm of a sound effect to be played.
// If the sound is not audible, returns a 0.
// Otherwise, modifies parameters and returns 1.
//
int S_AdjustSoundParams ( mobj_t*       listener,
                          mobj_t*       source,
                          int*          vol,
                          int*          sep,
                          int*          pitch )
{
    fixed_t     approx_dist;
    fixed_t     adx;
    fixed_t     ady;
    angle_t     angle;

    // calculate the distance to sound origin
    //  and clip it if necessary
    adx = abs(listener->x - source->x);
    ady = abs(listener->y - source->y);

    // From _GG1_ p.428. Appox. eucledian distance fast.
    approx_dist = adx + ady - ((adx < ady ? adx : ady)>>1);

    if (gamemap != 8
        && approx_dist > S_CLIPPING_DIST)
    {
        return 0;
    }

    // angle of source to listener
    angle = R_PointToAngle2(listener->x,
                            listener->y,
                            source->x,
                            source->y);

    if (angle > listener->angle)
        angle = angle - listener->angle;
    else
        angle = angle + (0xffffffff - listener->angle);

    angle >>= ANGLETOFINESHIFT;

    // stereo separation
    *sep = 128 - (FixedMul(S_STEREO_SWING,finesine[angle])>>FRACBITS);

    // volume calculation
    if (approx_dist < S_CLOSE_DIST)
    {
        // added 2-2-98 SfxVolume is now hardware volume
        *vol = 255; //snd_SfxVolume;
    }
    // removed hack here for gamemap==8 (it made far sound still present)
    else
    {
        // distance effect
        *vol = (15
                * ((S_CLIPPING_DIST - approx_dist)>>FRACBITS))
            / S_ATTENUATOR;
    }

    return (*vol > 0);
}




//
// S_getChannel :
//   If none available, return -1.  Otherwise channel #.
//
int S_getChannel( void*         origin,
                  sfxinfo_t*    sfxinfo )
{
    // channel number to use
    int         cnum;

    channel_t*  c;

    // Find an open channel
    for (cnum=0 ; cnum<cv_numChannels.value ; cnum++)
    {
        if (!channels[cnum].sfxinfo)
            break;
        else if (origin &&  channels[cnum].origin ==  origin)
        {
            S_StopChannel(cnum);
            break;
        }
    }

    // None available
    if (cnum == cv_numChannels.value)
    {
        // Look for lower priority
        for (cnum=0 ; cnum<cv_numChannels.value ; cnum++)
            if (channels[cnum].sfxinfo->priority >= sfxinfo->priority) break;

        if (cnum == cv_numChannels.value)
        {
            // FUCK!  No lower priority.  Sorry, Charlie.
            return -1;
        }
        else
        {
            // Otherwise, kick out lower priority.
            S_StopChannel(cnum);
        }
    }

    c = &channels[cnum];

    // channel is decided to be cnum.
    c->sfxinfo = sfxinfo;
    c->origin = origin;

    return cnum;
}
