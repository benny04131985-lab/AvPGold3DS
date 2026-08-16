#include "unaligned.h"
#include "3dc.h"
#include "ourasert.h"
#include "psndplat.h"
#include "jsndsup.h"
#include "mempool.h"
#include "scream.h"
#include "dxlog.h"
#include "avp_menus.h"

#include <stdio.h>

#ifdef __3DS__
/*
 * YEET33C dynamic sound diagnostics.
 */
static int avp3ds_scream_diag_budget = 32;
#endif

extern "C"
{
extern DISPLAYBLOCK* Player;


struct ScreamSound
{
	struct loaded_sound const * sound_loaded;
	int pitch;
	int volume;	
};

struct ScreamSoundCategory
{
	int num_sounds;
	ScreamSound* sounds;

	SOUNDINDEX last_sound;
};
 
struct ScreamVoiceType
{
	ScreamSoundCategory* category;
};

struct CharacterSoundEffects
{
	int num_voice_types;
	int num_voice_cats;
	ScreamVoiceType* voice_types;
	SOUNDINDEX global_last_sound;

	void PlaySound(int VoiceType,int SoundCategory,int PitchShift,int* ExternalRef,VECTORCH* Location);
	void UnloadSounds();
	void LoadSounds(const char* filename,const char* directory);
};

static CharacterSoundEffects MarineSounds={0,0,0,SID_NOSOUND};
static CharacterSoundEffects AlienSounds={0,0,0,SID_NOSOUND};
static CharacterSoundEffects PredatorSounds={0,0,0,SID_NOSOUND};
static CharacterSoundEffects QueenSounds={0,0,0,SID_NOSOUND};


//static int num_voice_types=0;
//static int num_voice_cats=0;
//static ScreamVoiceType* voice_types=0;
//static SOUNDINDEX global_last_sound;


#if ALIEN_DEMO
#define ScreamFilePath "alienfastfile/"
#elif LOAD_SCREAMS_FROM_FASTFILES
#define ScreamFilePath "fastfile/"
#else
#define ScreamFilePath "sound/"
#endif


void CharacterSoundEffects::LoadSounds(const char* filename,const char* directory)
{
	#ifdef __3DS__
        int avp3ds_db_requested = 0;
        int avp3ds_db_loaded = 0;
        int avp3ds_db_failed = 0;
#endif

        if(voice_types) return;

	char path[100]=ScreamFilePath;
	strcat(path,filename);

#ifdef __3DS__
        printf("YEET33C DBOPEN %s\n", path);
#endif

	FILE *file = OpenGameFile(path, FILEMODE_READONLY, FILETYPE_PERM);
	if(file==NULL)
        {
                LOGDXFMT(("Failed to open %s",path));
#ifdef __3DS__
                printf("YEET33C DB OPEN FAIL %s\n", path);
#endif
                return;
        }

	char* buffer;
	int file_size;

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);
	
	buffer=new char[file_size+1];
	
	fread(buffer, file_size, 1, file);
	fclose(file);

	if(strncmp("MARSOUND",buffer,8))
        {
#ifdef __3DS__
                printf("YEET33C DB BAD HEADER %s\n", filename);
#endif
                delete [] buffer;
                return;
        }

	char* bufpos=buffer+8;

	num_voice_types=*(unaligned_s32*)bufpos;
	bufpos+=4;
	num_voice_cats=*(unaligned_s32*)bufpos;
	bufpos+=4;
	
	voice_types=(ScreamVoiceType*) PoolAllocateMem(num_voice_types * sizeof(ScreamVoiceType));
	
	char wavpath[200];
	strcpy(wavpath,directory);
	char* wavname=&wavpath[strlen(wavpath)];
	
	for(int i=0;i<num_voice_types;i++)	
	{
		voice_types[i].category=(ScreamSoundCategory*) PoolAllocateMem( num_voice_cats * sizeof(ScreamSoundCategory));
		for(int j=0;j<num_voice_cats;j++)
		{
			ScreamSoundCategory* cat=&voice_types[i].category[j];
			cat->last_sound=SID_NOSOUND;
			cat->num_sounds=*(unaligned_s32*)bufpos;
			bufpos+=4;

			if(cat->num_sounds)
			{
				cat->sounds=(ScreamSound*) PoolAllocateMem(cat->num_sounds * sizeof(ScreamSound));
			}
			else
			{
				cat->sounds=0;
			}

			for(int k=0;k<cat->num_sounds;)
			{
				ScreamSound * sound=&cat->sounds[k];

				strcpy(wavname,bufpos);
				bufpos+=strlen(bufpos)+1;

				sound->pitch=*(unaligned_s32*)bufpos;
				bufpos+=4;
				sound->volume=*(unaligned_s32*)bufpos;
				bufpos+=4;

				#ifdef __3DS__
                                avp3ds_db_requested++;
#endif

                                sound->sound_loaded=GetSound(wavpath);

                                if(sound->sound_loaded)
                                {
#ifdef __3DS__
                                        avp3ds_db_loaded++;
#endif
                                        k++;
                                }
                                else
                                {
#ifdef __3DS__
                                        avp3ds_db_failed++;

                                        if(avp3ds_db_failed <= 6)
                                        {
                                                printf(
                                                    "YEET33C WAV FAIL %s\n",
                                                    wavpath);
                                        }
#endif
                                        cat->num_sounds--;
                                }

			}

		}
	}

	#ifdef __3DS__
        printf(
            "YEET33C DB %s t=%d c=%d\n",
            filename,
            num_voice_types,
            num_voice_cats);

        printf(
            "YEET33C DB r=%d ok=%d f=%d\n",
            avp3ds_db_requested,
            avp3ds_db_loaded,
            avp3ds_db_failed);
#endif

        delete [] buffer;
		
}

void CharacterSoundEffects::UnloadSounds()
{
	if(!voice_types) return;
	#if !NEW_DEALLOCATION_ORDER
	for(int i=0;i<num_voice_types;i++)
	{
		for(int j=0;j<num_voice_cats;j++)
		{
			ScreamSoundCategory* cat=&voice_types[i].category[j];
			for(int k=0;k<cat->num_sounds;k++)
			{
				LoseSound(cat->sounds[k].sound_loaded);
			}
		}
	}
	#endif
	voice_types=0;
	num_voice_types=0;
	num_voice_cats=0;
}



void CharacterSoundEffects::PlaySound(int VoiceType,int SoundCategory,int PitchShift,int* ExternalRef,VECTORCH* Location)
{
#ifdef __3DS__
        if(avp3ds_scream_diag_budget > 0)
        {
                printf(
                    "YEET33C CALL v=%d c=%d ref=%d\n",
                    VoiceType,
                    SoundCategory,
                    ExternalRef ? *ExternalRef : -999);

                avp3ds_scream_diag_budget--;
        }
#endif

        //	GLOBALASSERT(Location);
	
	if(!voice_types)
        {
#ifdef __3DS__
                printf("YEET33C BLOCK no-db\n");
#endif
                return;
        }
	//make sure the values are within bounds
	if(VoiceType<0 || VoiceType>=num_voice_types) return;
	if(SoundCategory<0 || SoundCategory>=num_voice_cats) return;
	if(ExternalRef)
        {
                if(*ExternalRef!=SOUND_NOACTIVEINDEX)
                {
#ifdef __3DS__
                        printf(
                            "YEET33C BLOCK ref=%d\n",
                            *ExternalRef);
#endif
                        //already playing a sound
                        return;
                }
        }

	#if 0
	if(Location)
	{
		//need to make sure this sound is close enough to be heard
		VECTORCH seperation=Player->ObWorld;
		SubVector(Location,&seperation);
		
		//(default sound range is 32 metres)
		if(Approximate3dMagnitude(&seperation)>32000)
		{
			//too far away . don't bother playing a sound.
			return;
		}
	}
	#endif

	ScreamSoundCategory* cat=&voice_types[VoiceType].category[SoundCategory];
	//make sure there are some sound for this category
	if(!cat->num_sounds)
        {
#ifdef __3DS__
                printf(
                    "YEET33C BLOCK empty v=%d c=%d\n",
                    VoiceType,
                    SoundCategory);
#endif
                return;
        }

	int index=FastRandom()% cat->num_sounds;
	int num_checked=0;

	//pick a sound , trying to avoid the last one picked
	if(cat->num_sounds>2)
	{
		while(num_checked<cat->num_sounds)
		{
			SOUNDINDEX sound_ind=(SOUNDINDEX)cat->sounds[index].sound_loaded->sound_num;
			if(sound_ind!=cat->last_sound && sound_ind!=global_last_sound) break;
			
			index++;
			num_checked++;
			if(index==cat->num_sounds) index=0;	
		}
	}

	ScreamSound* sound=&cat->sounds[index];

	int pitch=sound->pitch+PitchShift;

#ifdef __3DS__
        if(avp3ds_scream_diag_budget > 0)
        {
                printf(
                    "YEET33C GO sn=%d vol=%d p=%d\n",
                    sound->sound_loaded->sound_num,
                    sound->volume,
                    pitch);

                avp3ds_scream_diag_budget--;
        }
#endif

	if(Location)
		Sound_Play((SOUNDINDEX)sound->sound_loaded->sound_num,"dvpe",Location,sound->volume,pitch,ExternalRef);
	else
		Sound_Play((SOUNDINDEX)sound->sound_loaded->sound_num,"vpe",sound->volume,pitch,ExternalRef);

	//take note of the last sound played
	global_last_sound=cat->last_sound=(SOUNDINDEX)sound->sound_loaded->sound_num;
}



void UnloadScreamSounds()
{
	MarineSounds.UnloadSounds();
	AlienSounds.UnloadSounds();
	PredatorSounds.UnloadSounds();
	QueenSounds.UnloadSounds();
}


void LoadMarineScreamSounds()
{
	MarineSounds.LoadSounds("marsound.dat","npc\\marinevoice\\");
}
void LoadAlienScreamSounds()
{
	AlienSounds.LoadSounds("aliensound.dat","npc\\alienvoice\\");
}
void LoadPredatorScreamSounds()
{
	PredatorSounds.LoadSounds("predsound.dat","npc\\predatorvoice\\");
}

void LoadQueenScreamSounds()
{
	QueenSounds.LoadSounds("queensound.dat","npc\\queenvoice\\");
}


void PlayMarineScream(int VoiceType,int SoundCategory,int PitchShift,int* ExternalRef,VECTORCH* Location)
{
	MarineSounds.PlaySound(VoiceType,SoundCategory,PitchShift,ExternalRef,Location);
}
void PlayAlienSound(int VoiceType,int SoundCategory,int PitchShift,int* ExternalRef,VECTORCH* Location)
{
	AlienSounds.PlaySound(VoiceType,SoundCategory,PitchShift,ExternalRef,Location);
}
void PlayPredatorSound(int VoiceType,int SoundCategory,int PitchShift,int* ExternalRef,VECTORCH* Location)
{
	PredatorSounds.PlaySound(VoiceType,SoundCategory,PitchShift,ExternalRef,Location);
}

void PlayQueenSound(int VoiceType,int SoundCategory,int PitchShift,int* ExternalRef,VECTORCH* Location)
{
	QueenSounds.PlaySound(VoiceType,SoundCategory,PitchShift,ExternalRef,Location);
}



};
