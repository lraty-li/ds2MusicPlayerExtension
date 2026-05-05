## Prompt:
2026/4/22 08:22:05

翻译：
The player object bears a ResourceName and a ResourceHash for a track (one for full, one for preview), by the Wwise documentation, this could be:

**Implicit IDs** = ResourceHash?
objects that are accessible via the Sound Engine's SDK functions (e.g. Event, State, Switch).

Because these objects may be referred to by SDK functions by name, these objects are assigned specific ShortID values. The ShortIDs are generated from a hash of the object's name. Because of this:

    No two named objects may have the same name.
    No two named objects may have the same ShortID.

As with the explicit ShortIDs (above), objects in this scope are grouped by type. Examples of object types are:

    Events
    Switches
    States
    RTPCs

Because the ShortIDs are not random but rather implicitly determined by the object's name, they are not tracked in the project's Work Units. If we look back at the event Play_Hello's Work Unit entry above, we see that the Event Play_Hello does not have a ShortID entry.

(Source: https://www.audiokinetic.com/en/public-library/2025.1.6_9117/?source=Help&id=managing_media_asset_ids)

Edit, it's not the implicit ID

But I'm still not shure, how/where the hash is stored on the final audio file...

## Prompt:
2026/4/22 08:24:00

Oh that's a really good catch. I was working off my factory hook seeing LocalizedSimpleSoundResource come through but you're right it's probably dialogue stuff — the music player's sample path might not even go through that factory. My bad on the assumption. On the hash side, earlier I'd noted (in my scratch notes) that the music player's SoundResource   has the resource NAME at +0xB0 as a Decima String (e.g. "sd_music_player_11_Minus_Sixty_One" for track 0), but the runtime event ID Wwise actually posts (2147876058 aka 0x8005FCDA for the sample play, per-track unique) doesn't match GetIDFromString on that name — GetIDFromString("sd_music_player_11_Minus_Sixty_One") returns 4231625949, which never appears in any PostEvent I captured. So the player isn't hashing the name at runtime to get the event ID; the event ID   has to be stored somewhere on the resource itself as a precomputed uint32, and I haven't found where. Candidates I'd look at: right after the +0xB0 string pointer in SoundResource (maybe +0xB8 or +0xBC), and the Events array at +0x290 on GraphSoundResource (the SoundResource subclass). If you're pulling these out with Odradek and you see a bare uint32   field named something like "ResourceHash" or "EventID" or "WwiseID", that's very likely what I'm looking for. And if you can confirm whether music tracks are GraphSoundResources specifically (vs plain SoundResources), that would save me a lot of guessing too

## Prompt:
2026/4/22 08:26:14

翻译：
I guess I found the EventID  

DS2MusicPlayerTrackResource  
   ↓  
GraphSoundResource   
(one for full, one for trial)  
   ↓  
GraphProgram  
   ↓  
ExposedDataResource  
   ↓  
NodeConstantsResource  
   ↓  
Parameters:  
[0] = WwiseID  
[1] = SoundGroup  
  
DefaultSoftLinkedObjects  
[0] WwiseID = 3056202008  
[1] SoundGroup = Sound FX?!

## Prompt:
2026/4/22 08:27:44

My theory now is that this is an EventID inside a bank, but I'm not shure how the game would get an EventID so fast...maybe it just happens?

## Prompt:
2026/4/22 08:28:03

Makes sense and that actually explains my earlier confusion, i tried GetIDFromString on the resource name (e.g.  "sd_music_player_11_Minus_Sixty_One") and the result never matched anything PostEvent was actually posting. So the  game isn't hashing the name at runtime; the ShortID is precomputed and baked into the resource somewhere, which  matches what you're seeing with the DefaultSoftLinkedObjects[0] = 3056202008. When you say "how does the game get it  so fast" — i think that's literally it, the value is just a uint32 sitting in the WwiseEvent object at load time, no  lookup needed, the game hands it straight to PostEvent. For bank extraction, you're going to want wwiser, it'll crack  banks to txtp and show you every event by name + ID in a way grep will find your 3056202008 in seconds. While you're  doing that i'll scan memory around one of my cloned tracks' GraphSoundResource for the specific uint32 value  3056202008 — if i find it at a stable offset that's my write target

## Prompt:
2026/4/22 08:29:27

i parsed the bank yesterday, and WemID in WwiseWemResource corresponds to wem id inside the bank

## Prompt:
2026/4/22 08:30:02

**MusicPlayer Structure:**

DS2MusicPlayerTrackResource
   ↓
GraphSoundResource (one for full, one for trial)
-- GraphProgram
   -- ExposedDataResource
       -- NodeConstantsResource
            Parameters:
                [0] = "WwiseID"
                [1] = "SoundGroup"
            DefaultSoftLinkedObjects:
                [0] WwiseID
                [1] SoundGroup

**Events**
The WwiseID is an EventID within a WwiseBankResource (.bnk Data inside Data.BankData), which turns into the final mix of multiple referenced WEM files (via WEMids). The WEM files can be within the bank, but also outside, as a WwiseWEMResource.

**Bank References**
As WwiseBankResources can have Data.BankData AND WEMID/WwiseWEMResource associations, one can assume that if a bank uses outside WwiseWEMResources, they are referenced there too? 

**Full and Trial**
The Full and Trial versions exist as two Wwise Events inside a Bank, the WEM files seem to be final mixes of the track as a WEM (one long, one short)
As observed, the trial WEM lives inside the graph, while the full track lives inside the bank (where the event is?) Sounds reasonable, as the game then can always play previews and only load the full track when someone wants to listen to it (for me, it's always  🥹 )

**Minus Sixty One**
*Have I ever really loved someone?
Yeah, this song, otherwise I wouldn't give it so much attention* 😂 

DS2MusicPlayerTrackResource 1180:1954, yada yada yada, boils down to:
FULL: GraphSoundResource: 1180:3710 -> WwiseID 3056202008
WwiseBankResource 1210:4633 = Bank 727071332
Event 3056202008
- Link to 676622012.wem (is WEM INSIDE BNK!)
- PATH / CAkEvent[10278] 3056202008 / CAkActionPlay[10277] 709017358

TRIAL: GraphSoundResource: 1180:6054 -> WwiseID 1633704605
WwiseBankResource 1210:4633 = Bank 727071332
Event 1633704605
- Link to 378574806.wem (is WwiseWEMResource 1210:336! )
- PATH / CAkEvent[10035] 1633704605 / CAkActionPlay[10034] 1052467820

## Prompt:
2026/4/22 08:37:07

I can send you the TXTPs of full and trial if you need them, but you can also find it in the bank
Full: 727071332-10278-event.txtp
Trial: 727071332-10035-event.txtp

Both are -basically - playing the WEM

## Prompt:
2026/4/22 08:41:09

Follow up question while you're digging: I traced the chain in memory and confirmed the WwiseID uint32 lives 5 pointer hops deep (Track+0xB0 -> +0x1D0 -> +0x218 -> +0x48 -> +0x308 on my build), and patching it works, BUT the cloned track shares all that downstream memory with the source track, so writing to it also clobbers the OG, which I don't want. I need to deep-clone the whole chain per custom track instead. To do that cleanly I need struct info you probably already have from the RTTI definitions: 1. Size in bytes of GraphSoundResource, GraphProgram, ExposedDataResource, NodeConstantsResource, and WwiseEvent (so I can allocate + memcpy each) 2. Offset of the outgoing pointer at each step (GraphSoundResource to GraphProgram, GraphProgram to ExposedDataResource, ExposedDataResource to NodeConstantsResource), plus the offset and array layout of DefaultSoftLinkedObjects inside NodeConstantsResource, plus the offset of the uint32 WwiseID inside the WwiseEvent object 3. Anything I'll break by naive cloning, like a refcount I need to bump or a registration with the streaming system / RTTI I need to redo Basically I want to allocate new copies of each struct, point them at each other in chain order, put my custom WwiseID at the final uint32 field, and swap the cloned track's +0xB0 ref to point at my brand new GraphSoundResource so the OG chain never gets touched. If you can share those struct layouts from Odradek I would be so thankful....

## Prompt:
2026/4/22 08:43:43

翻译
Can't you create the objects 
starting from the GraphSoundResource, append and load a new a bank with event, wem and a WwiseWemResource and then just reference it in the new DSPlayerTrackResource so it uses the new objects? Or is that thought very naive? I'm no expert in mem stuff

## Prompt:
2026/4/22 08:44:00

Not naive at all, that's actually the cleaner path than cloning. Fresh build means I don't have to figure out which fields are stateful or bump any refcounts on the original, I just make minimal valid objects and wire them up.

On the bank/WEM side I've got most of this working already:

- Custom bank loads cleanly via LoadBankMemoryCopy (it's the captured audio bank with appended HIRC items for my custom event/action/sound)
- SetMedia provides my custom WEM data for my custom source IDs
- PostEvent of my custom event IDs returns a valid playingId

So the Wwise half is fine. The part I haven't touched is the Decima-side WwiseWemResource that you mentioned. Do the music player's GraphSoundResource or its downstream objects actually look up a WwiseWemResource object at play time (in which case I do need to build one), or is that purely an asset-pipeline thing that gets flattened into the bank itself and the game just calls PostEvent with the WwiseID and Wwise handles the rest?

Same field offsets/sizes question applies to a fresh build as to a clone basically. To build a minimal GraphSoundResource (plus whatever downstream objects it needs) I need to know how big each is and where the outgoing pointer lives. Plus one extra: do these Decima resources need to be registered with anything (RTTI system, streaming manager, a type table) or can I just allocate them as raw memory, set the vtable pointer, fill in the fields, and the game is happy as long as TrackResource ends up pointing at something valid? (I do not expect you to know this I'm just thinking out loud...) :D

## Prompt:
2026/4/22 08:56:45

翻译：
WwiseWEMResources are - as far as I saw - in our case just referenced in the WwiseBankResource object:    
    
WwiseBankInstance    
-- Banks[]    
    -- WwiseBankResource    
         BankData = .bnk (.bnk contains events and may contain wems)    
         WemIDs (Array) <-> Wems (WwiseWEMResource references)    
    
From Odradek's side of things, the TrackResource just points to a Wwise event via WwiseID. Maybe when the game loads the banks, all the event IDs are known so the system can find them just with the ID?



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)