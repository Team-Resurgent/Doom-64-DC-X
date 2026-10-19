#include <kos.h>
#include <dc/sound/sound.h>
#include "sndwav.h"

/* s_sound.c */
#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "sounds.h"

int activ = 0;
wav_stream_hnd_t cur_hnd = SND_STREAM_INVALID;

void S_RemoveOrigin(mobj_t *origin)
{
	(void)origin;
}

void S_ResetSound(void)
{
}

void S_UpdateSounds(void)
{
}

extern void W_DrawLoadScreen(char *what, int current, int total);

sfxhnd_t sounds[NUMSFX];

#define stringed(sfxname) #sfxname

void *sndptr;

#define setsfx(sn)											\
	sprintf(fnbuf, "%s/sfx/%s.wav", fnpre, stringed(sn));	\
	fs_load(fnbuf, &sndptr);								\
	sounds[sn] = snd_sfx_load_buf((char *)sndptr);			\
	if (sndptr) free(sndptr);								\
	W_DrawLoadScreen("Sounds", sn, NUMSFX - 24)

void init_all_sounds(void)
{
	sounds[0] = 0;
	dbglog_set_level(DBG_INFO);
	setsfx(sfx_punch);
	setsfx(sfx_spawn);
	setsfx(sfx_explode);
	setsfx(sfx_implod);
	setsfx(sfx_pistol);
	setsfx(sfx_shotgun);
	setsfx(sfx_plasma);
	setsfx(sfx_bfg);
	setsfx(sfx_sawup);
	setsfx(sfx_sawidle);
	setsfx(sfx_saw1);
	setsfx(sfx_saw2);
	setsfx(sfx_missile);
	setsfx(sfx_bfgexp);
	setsfx(sfx_pstart);
	setsfx(sfx_pstop);
	setsfx(sfx_doorup);
	setsfx(sfx_doordown);
	setsfx(sfx_secmove);
	setsfx(sfx_switch1);
	setsfx(sfx_switch2);
	setsfx(sfx_itemup);
	setsfx(sfx_sgcock);
	setsfx(sfx_oof);
	setsfx(sfx_telept);
	setsfx(sfx_noway);
	setsfx(sfx_sht2fire);
	setsfx(sfx_sht2load1);
	setsfx(sfx_sht2load2);
	setsfx(sfx_plrpain);
	setsfx(sfx_plrdie);
	setsfx(sfx_slop);
	setsfx(sfx_possit1);
	setsfx(sfx_possit2);
	setsfx(sfx_possit3);
	setsfx(sfx_posdie1);
	setsfx(sfx_posdie2);
	setsfx(sfx_posdie3);
	setsfx(sfx_posact);
	setsfx(sfx_dbpain1);
	setsfx(sfx_dbpain2);
	setsfx(sfx_dbact);
	setsfx(sfx_scratch);
	setsfx(sfx_impsit1);
	setsfx(sfx_impsit2);
	setsfx(sfx_impdth1);
	setsfx(sfx_impdth2);
	setsfx(sfx_impact);
	setsfx(sfx_sargsit);
	setsfx(sfx_sargatk);
	setsfx(sfx_sargdie);
	setsfx(sfx_bos1sit);
	setsfx(sfx_bos1die);
	setsfx(sfx_headsit);
	setsfx(sfx_headdie);
	setsfx(sfx_skullatk);
	setsfx(sfx_bos2sit);
	setsfx(sfx_bos2die);
	setsfx(sfx_pesit);
	setsfx(sfx_pepain);
	setsfx(sfx_pedie);
	setsfx(sfx_bspisit);
	setsfx(sfx_bspidie);
	setsfx(sfx_bspilift);
	setsfx(sfx_bspistomp);
	setsfx(sfx_fattatk);
	setsfx(sfx_fattsit);
	setsfx(sfx_fatthit);
	setsfx(sfx_fattdie);
	setsfx(sfx_bdmissile);
	setsfx(sfx_skelact);
	setsfx(sfx_tracer);
	setsfx(sfx_dart);
	setsfx(sfx_dartshoot);
	setsfx(sfx_cybsit);
	setsfx(sfx_cybdth);
	setsfx(sfx_cybhoof);
	setsfx(sfx_metal);
	setsfx(sfx_door2up);
	setsfx(sfx_door2dwn);
	setsfx(sfx_powerup);
	setsfx(sfx_laser);
	setsfx(sfx_electric);
	setsfx(sfx_thndrlow);
	setsfx(sfx_thndrhigh);
	setsfx(sfx_quake);
	setsfx(sfx_darthit);
	setsfx(sfx_rectact);
	setsfx(sfx_rectatk);
	setsfx(sfx_rectdie);
	setsfx(sfx_rectpain);
	setsfx(sfx_rectsit);
}

void S_Init(void)
{
	int wi_rv = wav_init();
	if (!wi_rv)
		dbgio_printf("could not wav_init\n");

	init_all_sounds();

	cur_hnd = SND_STREAM_INVALID;
	S_SetSoundVolume(menu_settings.SfxVolume);
}

float soundscale = 1.0f;

void S_SetSoundVolume(int volume)
{
	soundscale = (float)volume / 100.0f;
	if (plasma_loop_channel != -1) {
		P_StopElectricLoop();
		P_StartElectricLoop();
	}
}

void S_SetMusicVolume(int volume)
{
	int sleeps = 0;

	if (cur_hnd == SND_STREAM_INVALID) {
		dbgio_printf("setmusvol invalid handle\n");
		return;
	}

	while (!wav_is_playing() && sleeps < 100) {
		sleeps++;
		thd_sleep(50);
	}

	if (sleeps < 100)
		wav_volume((volume * 255)/100);
	else
		dbgio_printf("timed out on wavisplaying\n");
}

int music_sequence;
char itname[256];

extern int from_menu;

void S_StartMusic(int mus_seq)
{
	if (disabledrawing) {
		music_sequence = 0;
		activ = 0;
		return;
	}

	music_sequence = mus_seq;

	char *name = NULL;
	switch (mus_seq) {
	case 96:
		name = "musamb04";
		break;

	case 97:
		name = "musamb05";
		break;

	case 105:
		name = "musamb13";
		break;

	case 104:
		name = "musamb12";
		break;

	case 101:
		name = "musamb09";
		break;

	case 107:
		name = "musamb15";
		break;

	case 108:
		name = "musamb16";
		break;

	case 110:
		name = "musamb18";
		break;

	case 95:
		name = "musamb03";
		break;

	case 98:
		name = "musamb06";
		break;

	case 99:
		name = "musamb07";
		break;

	case 102:
		name = "musamb10";
		break;

	case 93:
		name = "musamb01";
		break;

	case 106:
		name = "musamb14";
		break;

	case 111:
		name = "musamb19";
		break;

	case 103:
		name = "musamb11";
		break;

	case 94:
		name = "musamb02";
		break;

	case 100:
		name = "musamb08";
		break;

	case 112:
		name = "musamb20";
		break;

	case 109:
		name = "musamb17";
		break;

	case 113:
		name = "musfinal";
		break;

	case 114:
		name = "musdone";
		break;

	case 115:
		name = "musintro";
		break;

	case 116:
		name = "mustitle";
		break;

	default:
		I_Error("unknown sequence %d\n", mus_seq);
		music_sequence = 0;
		activ = 0;
		return;
	}

	int looping = 1;

	if ((!from_menu) && (gamemap > 40) && !((mus_seq >= 113) && (mus_seq <= 116))) {
		sprintf(itname, "%s/mus/e1m%d.adpcm", fnpre, gamemap-40);
	} else {
		sprintf(itname, "%s/mus/%s.adpcm", fnpre, name);
		if ((mus_seq == 115) || (mus_seq == 114)) {
			looping = 0;
		}
	}

	cur_hnd = wav_create(itname, looping);

	if (cur_hnd == SND_STREAM_INVALID) {
#if RANGECHECK
		dbgio_printf("Could not create wav %s\n", itname);
#endif
		music_sequence = 0;
		activ = 0;
		return;
	}

	wav_play();

	S_SetMusicVolume(menu_settings.MusVolume);

	activ = 1;
}

void S_StopMusic(void)
{
	music_sequence = 0;
	if (cur_hnd != SND_STREAM_INVALID) {
		wav_destroy();
 		cur_hnd = SND_STREAM_INVALID;
 	}
}

void S_PauseSound(void)
{
}

void S_ResumeSound(void)
{
}

void S_StopSound(mobj_t *origin, int seqnum)
{
(void)origin;
(void)seqnum;
}

void S_StopAll(void)
{
	snd_sfx_stop_all();

	S_StopMusic();
}

#define SND_INACTIVE 0
#define SND_PLAYING 1

int S_SoundStatus(int seqnum)
{
	(void)seqnum;
	return activ;
}

int S_StartSound(mobj_t *origin, int sound_id)
{
	int vol;
	int pan;

#if RANGECHECK
	if (sound_id < 0 || sound_id > sfx_rectsit)
		I_Error("invalid sound_id %d\n", sound_id);
#endif

	if (disabledrawing == false) {
		if (origin && (origin != cameratarget)) {
			if (!S_AdjustSoundParams(cameratarget, origin, &vol,  &pan))
				return -1;
		} else {
			vol = 124;
			pan = 128;
		}

		return snd_sfx_play(sounds[sound_id], (int)((float)vol * soundscale), pan);
	}
	return -1;
}

#define S_CLIPPING_DIST (1700)
#define S_MAX_DIST (127 * S_CLIPPING_DIST)
#define S_CLOSE_DIST (200)
#define S_ATTENUATOR (S_CLIPPING_DIST - S_CLOSE_DIST)
#define S_STEREO_SWING (96)

int S_AdjustSoundParams(mobj_t *listener, mobj_t *origin, int *vol, int *pan)
{
	fixed_t approx_dist;
	angle_t angle;
	int tmpvol;
	int tmppan;

	approx_dist = P_AproxDistance(listener->x - origin->x, listener->y - origin->y);
	approx_dist >>= FRACBITS;

	if (approx_dist > S_CLIPPING_DIST)
		return 0;

	tmppan = 128;

	if ((listener->x != origin->x) || (listener->y != origin->y)) {
		/* angle of source to listener */
		angle = R_PointToAngle2(listener->x, listener->y, origin->x, origin->y);

		if (angle <= listener->angle)
			angle += 0xffffffff;

		angle -= listener->angle;

		/* stereo separation */
		tmppan -= ((finesine[angle >> ANGLETOFINESHIFT] * S_STEREO_SWING) >> FRACBITS);
	}

	/* volume calculation */
	if (approx_dist < S_CLOSE_DIST) {
		tmpvol = 124; // all 124 used to be 127
	} else {
		/* distance effect */
		approx_dist = -approx_dist; /* set neg */
		tmpvol = (((approx_dist << 7) - approx_dist) + S_MAX_DIST) / S_ATTENUATOR;
	}

	if (tmpvol > 124)
		tmpvol = 124;

	*vol = tmpvol;
	*pan = tmppan;
	return (tmpvol > 0);
}
