#!/usr/bin/env python3
"""
PCM 90 IR Mapping — Maps all PCM 90 impulse responses to Velvet 90 preset entries.

Generates a list of (ir_path, preset_name, velvet90_category, velvet90_mode) tuples
for all available IR files, based on the PCM 90 preset documentation.
"""

import os

PCM90_IR_BASE = os.environ.get(
    'PCM90_IR_BASE',
    '/Users/marckorte/Downloads/PCM 90 Impulse Set',
)

# Velvet 90 mode indices (matching FDNReverb.h enum)
MODE_PLATE = 0
MODE_ROOM = 1
MODE_HALL = 2
MODE_CHAMBER = 3
MODE_CATHEDRAL = 4
MODE_AMBIENCE = 5
MODE_BRIGHT_HALL = 6
MODE_CHORUS_SPACE = 7
MODE_RANDOM_SPACE = 8
MODE_DIRTY_HALL = 9

MODE_NAMES = {
    0: 'Plate', 1: 'Room', 2: 'Hall', 3: 'Chamber', 4: 'Cathedral',
    5: 'Ambience', 6: 'Bright Hall', 7: 'Chorus Space', 8: 'Random Space', 9: 'Dirty Hall',
}

# ============================================================================
# PCM 90 P0: Halls — Mode assignments based on PDF descriptions
# ============================================================================
HALLS_MAP = {
    # Orchestral
    'Deep Blue (567)':       ('Deep Blue',       'Halls', MODE_HALL,       'All-purpose hall, moderate size/decay'),
    'Concert Hall (574)':    ('Concert Hall',     'Halls', MODE_HALL,       'Dense, classic digital hall'),
    'Medium Hall (569)':     ('Medium Hall',      'Halls', MODE_HALL,       'Natural medium-size hall'),
    'Small Hall (562)':      ('Small Hall',       'Halls', MODE_CHAMBER,    'Small hall, bright initial reverb'),
    'L Hall + Stage (570)':  ('Large Hall+Stage', 'Halls', MODE_HALL,       'Large hall with stage reflections'),
    'M Hall + Stage (571)':  ('Med Hall+Stage',   'Halls', MODE_HALL,       'Medium hall with stage reflections'),
    'S Hall + Stage (572)':  ('Small Hall+Stage', 'Halls', MODE_CHAMBER,    'Small hall with stage reflections'),
    'Gothic Hall (573)':     ('Gothic Hall',      'Halls', MODE_CATHEDRAL,  'Large, filtered, medium-bright hall of stone'),
    'Small Church (575)':    ('Small Church',     'Halls', MODE_CATHEDRAL,  'Small hall, no reflections, short decay'),
    # Vocal
    'Choir Hall (576)':      ('Choir Hall',       'Halls', MODE_HALL,       'Medium-sized space with lots of reflections'),
    'Vocal Hall (577)':      ('Vocal Hall',       'Halls', MODE_HALL,       'Medium-sized hall, short clear decay'),
    'Vocal Hall 2 (578)':    ('Vocal Hall 2',     'Halls', MODE_HALL,       'Fairly large hall, generous reverb decay'),
    'VocalConcert (579)':    ('Vocal Concert',    'Halls', MODE_HALL,       'Enormous, silky reflective room'),
    "Rise'n Hall (580)":     ("Rise'n Hall",      'Halls', MODE_HALL,       'Long ER rise, short decay'),
    "Good ol'Verb (584)":    ("Good Ol' Verb",    'Halls', MODE_HALL,       'Quick solution, well rounded reverb'),
    'Deep Verb (586)':       ('Deep Verb',        'Halls', MODE_HALL,       'Large, washy, chorused space'),
    'Vocal Magic (588)':     ('Vocal Magic',      'Halls', MODE_HALL,       'Lovely reverb with short decay'),
    'Wide Vox (590)':        ('Wide Vox',         'Halls', MODE_HALL,       'Close delays double the source, wide'),
    'Slap Hall (582)':       ('Slap Hall',        'Halls', MODE_ROOM,      'Slap initial double tap, dark'),
    # Live Sound
    'Live Arena (591)':      ('Live Arena',       'Halls', MODE_HALL,       'Very large hall, moderate decay'),
    'Real Hall (587)':       ('Real Hall',        'Halls', MODE_HALL,       'Small, bright sounding hall'),
    'Great Hall (593)':      ('Great Hall',       'Halls', MODE_HALL,       'Great hall reverb, works with all material'),
    'Brick Wallz (589)':     ('Brick Wallz',      'Halls', MODE_ROOM,      'Wide and abrupt sounding, gated'),
    'Cannon Gate (594)':     ('Cannon Gate',      'Halls', MODE_HALL,       'Medium-sized room, sharp medium long decay'),
    'Spatial Hall (581)':    ('Spatial Hall',      'Halls', MODE_HALL,       'Strange hall with LFO controlling spatial EQ'),
    'Nonlin Wrhse (595)':    ('Nonlin Warehouse', 'Halls', MODE_DIRTY_HALL, 'Large nonlinear reverb, like gated warehouse'),
    'Sizzle Hall (583)':     ('Sizzle Hall',      'Halls', MODE_BRIGHT_HALL,'Bright, close hall, medium short decay, live quality'),
    'Bright Hall (596)':     ('Bright Hall',      'Halls', MODE_BRIGHT_HALL,'Light reverb, great deal of high end'),
    'Utility Hall (585)':    ('Utility Hall',     'Halls', MODE_HALL,       'Large hall, very little HF content'),
    # Instrument
    'Horns Hall (592)':      ('Horns Hall',       'Halls', MODE_HALL,       'Very large space, ideal for horns'),
    'Snare Gate (561)':      ('Snare Gate',       'Halls', MODE_ROOM,      'Tight, gated hall for snares'),
    'Guitar Cave (597)':     ('Guitar Cave',      'Halls', MODE_ROOM,      'Long predelay with recirculating echoes'),
    'Drum Cave (598)':       ('Drum Cave',        'Halls', MODE_ROOM,      'Medium sized cave, short decay time'),
    'Saxy Hangar (599)':     ('Saxy Hangar',      'Halls', MODE_HALL,       'Airplane hangar for spacious sax'),
    'Gated Hall (600)':      ('Gated Hall',       'Halls', MODE_HALL,       'If possible to have a gated hall'),
    'For The Toms (601)':    ('For The Toms',     'Halls', MODE_HALL,       'Large, dense room reverb for toms'),
    'Synth Hall (602)':      ('Synth Hall',       'Halls', MODE_HALL,       'Chorused hall, long decay for synths'),
    'ShortReverse (603)':    ('Short Reverse',    'Halls', MODE_HALL,       'Short reverse reverb, quick build up'),
    'GtrBallad (604)':       ('Guitar Ballad',    'Halls', MODE_HALL,       'Medium-sized room, 2-second reverb'),
    # Custom
    'Tidal Hall (605)':      ('Tidal Hall',       'Halls', MODE_HALL,       'Strange hall with LFO controlling reverb HF cut'),
    'Dream Hall (606)':      ('Dream Hall',       'Halls', MODE_BRIGHT_HALL,'Bright, crystalline hall with subtle delay taps'),
    'PumpVerb (607)':        ('Pump Verb',        'Halls', MODE_HALL,       'Strange, semi-gated reverb with pumping'),
    'PanHall (608)':         ('Pan Hall',         'Halls', MODE_HALL,       'LFO patched to OutWidth, subtle sweeping'),
    'Utility Verb (564)':    ('Utility Verb',     'Halls', MODE_HALL,       'General, all purpose reverb'),
    'Museum Hall (565)':     ('Museum Hall',      'Halls', MODE_HALL,       'Reverberant hall like a large room in a museum'),
    'NonLinear#1 (560)':     ('NonLinear #1',     'Halls', MODE_DIRTY_HALL, 'Dense, medium long, nonlinear gated verb'),
    'Tap Brick (566)':       ('Tap Brick',        'Halls', MODE_ROOM,      'Very reflective sound, like pounding a brick wall'),
    'Gen. Concert (568)':    ('Gen. Concert',     'Halls', MODE_HALL,       'Generic concert hall, starting place'),
    'Gen. RHall (563)':      ('Gen. Random Hall', 'Halls', MODE_RANDOM_SPACE, 'Generic hall with random reflections'),
}

# ============================================================================
# PCM 90 P1: Rooms — Mode assignments
# ============================================================================
ROOMS_MAP = {
    # Instrument
    'Large Room (667)':      ('Large Room',       'Rooms', MODE_ROOM,      'Perfectly smooth listening room, high diffusion'),
    'Medium Room (668)':     ('Medium Room',      'Rooms', MODE_ROOM,      'Smaller version of Large Room'),
    'Small Room (669)':      ('Small Room',       'Rooms', MODE_ROOM,      'Tight, smooth and natural sounding room'),
    'Guitar Room (659)':     ('Guitar Room',      'Rooms', MODE_ROOM,      'Tight and punchy ambience, combining small sizes'),
    'Organ Room (670)':      ('Organ Room',       'Rooms', MODE_CHAMBER,   'Chamber/Room for organ and other keyboards'),
    'Large Chamber (671)':   ('Large Chamber',    'Rooms', MODE_CHAMBER,   'Smooth, large reverberant space using Shape and Spread'),
    'Small chamber (672)':   ('Small Chamber',    'Rooms', MODE_CHAMBER,   'Similar to Large Chamber with tighter Mid RT/size'),
    'Spinning Room (673)':   ('Spinning Room',    'Rooms', MODE_CHORUS_SPACE, 'Nice Ambience reverb with circular sweep of Out Width'),
    'Wide Chamber (674)':    ('Wide Chamber',     'Rooms', MODE_CHAMBER,   'Big, wide space with dark, somber effect'),
    'Tiled Room (675)':      ('Tiled Room',       'Rooms', MODE_ROOM,      'Incredibly sibilant and bright reverberant space'),
    # Vocal
    'Bright Vocal (676)':    ('Bright Vocal',     'Rooms', MODE_ROOM,      'Bit of predelay separates bright reverb from source'),
    'Vocal Space (677)':     ('Vocal Space',      'Rooms', MODE_ROOM,      'Short Mid RT and small Size — ideal for vocals'),
    'Vocal Amb (678)':       ('Vocal Ambience',   'Rooms', MODE_AMBIENCE,  'Short and soft, very realistic small room'),
    'Very Small Amb (679)':  ('Very Small Amb',   'Rooms', MODE_AMBIENCE,  'Just like Vocal Amb, but smaller and tighter'),
    'S VocalSpace (680)':    ('Sm Vocal Space',   'Rooms', MODE_ROOM,      'Bigger version of S VocalSpace'),
    'L VocalSpace (681)':    ('Lg Vocal Space',   'Rooms', MODE_ROOM,      'More spacious version of S Vocal Space'),
    'S Vocal Amb (682)':     ('Sm Vocal Amb',     'Rooms', MODE_AMBIENCE,  'Spacious version of S Vocal Amb, set to Studio A'),
    'L Vocal Amb (683)':     ('Lg Vocal Amb',     'Rooms', MODE_AMBIENCE,  'More spacious version of S Vocal Amb'),
    'AmbientSus (684)':      ('Ambient Sustain',  'Rooms', MODE_AMBIENCE,  'Bit of dry delay, sweet for vocals/instruments'),
    'Vocal Booth (705)':     ('Vocal Booth',      'Rooms', MODE_ROOM,      'Most confining of isolation booths'),
    # Live Sound
    'LargeSpace (694)':      ('Large Space',      'Rooms', MODE_ROOM,      'Designed for live sound reinforcement'),
    'Med. Space (685)':      ('Medium Space',     'Rooms', MODE_ROOM,      'Small, intimate setting, smooth reverb'),
    'Delay Space (686)':     ('Delay Space',      'Rooms', MODE_ROOM,      'Live sound with less dominating, punchier sound'),
    'BigBoom Room (663)':    ('Big Boom Room',    'Rooms', MODE_ROOM,      'Saturated bottom-heavy, dense reverb'),
    'Toght Space (687)':     ('Tight Space',      'Rooms', MODE_ROOM,      'Vibrancy and attitude with a gated feel'),
    'Reflect Room (688)':    ('Reflect Room',     'Rooms', MODE_ROOM,      'Super-saturated, atmospheric quality'),
    'Rock Room (689)':       ('Rock Room',        'Rooms', MODE_ROOM,      'Extremely bright live drum sound'),
    'Real Room (690)':       ('Real Room',        'Rooms', MODE_ROOM,      'Natural reverb for a live setting'),
    'Spatial Bass (691)':    ('Spatial Bass',      'Rooms', MODE_ROOM,      'Spatial EQ bass boost enhances lower frequencies'),
    'Great Room (692)':      ('Great Room',       'Rooms', MODE_ROOM,      'Warm smooth reverb of Real Room with more decay'),
    # Drums&Perc
    'Drum Room (696)':       ('Drum Room',        'Rooms', MODE_ROOM,      'Dark preset, dense saturated, for whole drum kit'),
    'Snare Trash (697)':     ('Snare Trash',      'Rooms', MODE_DIRTY_HALL,'Large room, short Mid RT, Spatial EQ bass boost'),
    'MetallicRoom (698)':    ('Metallic Room',    'Rooms', MODE_ROOM,      'Resonant drum preset, very small Size/Mid RT'),
    'Slap Place (699)':      ('Slap Place',       'Rooms', MODE_ROOM,      'Dark and wet reverb, medium room long reverb tail'),
    'PercussPlace (695)':    ('Percussion Place',  'Rooms', MODE_ROOM,      'Full and resonant reverb, accentuates transients'),
    'PercussRoom (700)':     ('Percussion Room',   'Rooms', MODE_ROOM,      'Similar to PercussPlace, slightly smaller'),
    'Room 4 Drums (701)':    ('Room 4 Drums',     'Rooms', MODE_ROOM,      'All you could ever want for drums — punch, attitude'),
    'Sloppy Place (702)':    ('Sloppy Place',     'Rooms', MODE_ROOM,      'Unnatural room reverb, enhances any drum track'),
    'WideSlapDrum (703)':    ('Wide Slap Drum',   'Rooms', MODE_ROOM,      'Special drum effect, narrow to wide, slap happy'),
    'InverseDrums (704)':    ('Inverse Drums',    'Rooms', MODE_HALL,      'Backwards effect, great as a special effect'),
    # Custom
    'PCM 60 Room (706)':     ('PCM 60 Room',      'Rooms', MODE_ROOM,      'Takes you back to the good old days'),
    'InverseRoom2 (708)':    ('Inverse Room 2',   'Rooms', MODE_HALL,      'Lots of options, backwards effect'),
    'BeeBee Slapz (707)':    ('BeeBee Slapz',     'Rooms', MODE_ROOM,      'Perfect for dreamy soundscapes, atmospheric'),
    'Storeroom (665)':       ('Storeroom',        'Rooms', MODE_ROOM,      'Customize how empty or full this storeroom is'),
    'Split Rooms (666)':     ('Split Rooms',      'Rooms', MODE_CHAMBER,   'Chamber/Room where a small and big room are mixed'),
    'Spatial Room (660)':    ('Spatial Room',      'Rooms', MODE_ROOM,      'Similar to SpinningRoom with different parameters'),
    'Hole Room (661)':       ('Hole Room',        'Rooms', MODE_ROOM,      'A dense concert hall'),
    'Storage Tank (662)':    ('Storage Tank',      'Rooms', MODE_ROOM,      'Metallic sound and bright resonance'),
    'StrangePlace (664)':    ('Strange Place',    'Rooms', MODE_CHORUS_SPACE, 'Super-tight concert hall with lots of spatial enhancement'),
    'Gen. Ambi. (693)':      ('Gen. Ambience',    'Rooms', MODE_AMBIENCE,  'Generic ambience, starting place'),
}

# ============================================================================
# PCM 90 P2: Plates — Mode assignments
# ============================================================================
PLATES_MAP = {
    # Instrument
    'Just Plate (710)':      ('Just Plate',       'Plates', MODE_PLATE,     'Basic plate for any kind of sound source'),
    'Rich Plate (734)':      ('Rich Plate',       'Plates', MODE_PLATE,     'An old standard, bright and diffuse'),
    'Gold Plate (735)':      ('Gold Plate',       'Plates', MODE_PLATE,     'Classic plate with long decay, medium high end'),
    'Plate4Brass (736)':     ('Plate For Brass',  'Plates', MODE_PLATE,     'A good plate for brass sounds'),
    'Rock Plate (737)':      ('Rock Plate',       'Plates', MODE_PLATE,     'Big boomy dark plate, moderate reverb tail'),
    'Eko Plate (738)':       ('Eko Plate',        'Plates', MODE_PLATE,     'Sweet combination of recirculating pre-echoes'),
    'A.Gtr Plate (719)':     ('Acoustic Gtr Plate','Plates', MODE_PLATE,    'Really smooth plate with slow reverb build'),
    'SynthLead (727)':       ('Synth Lead',       'Plates', MODE_PLATE,     'Medium bright plate with tempo delays for synth'),
    'Floyd Wash (728)':      ('Floyd Wash',       'Plates', MODE_PLATE,     'Big plate with long predelay and repeating echo'),
    'GtrPlate (729)':        ('Guitar Plate',     'Plates', MODE_PLATE,     'Moderate size, dark plate reverb for guitar'),
    # Vocal
    'Vocal Plate (730)':     ('Vocal Plate',      'Plates', MODE_PLATE,     'Short plate, low diffusion, solo vocal track'),
    'Vocal Plate2 (739)':    ('Vocal Plate 2',    'Plates', MODE_PLATE,     'Large plate, moderate decay for backing vocals'),
    'SmlVoxPlate (733)':     ('Small Vox Plate',  'Plates', MODE_PLATE,     'Small bright plate for vocals'),
    'VoclEkoPlate (740)':    ('Vocal Echo Plate', 'Plates', MODE_PLATE,     'Large dark plate, just the right amount of delay'),
    'Choir Plate (741)':     ('Choir Plate',      'Plates', MODE_PLATE,     'Large silky plate, long decay for background'),
    'Multi Vox (742)':       ('Multi Vox',        'Plates', MODE_PLATE,     'Small short plate for gang vocals'),
    'Bright Vox (743)':      ('Bright Vox Plate', 'Plates', MODE_PLATE,     'Large bright plate, long decay for various vocals'),
    'VoclEcho (732)':        ('Vocal Echo',       'Plates', MODE_PLATE,     'Silky smooth plate, moderate decay, recirculating'),
    'VocalTap (711)':        ('Vocal Tap',        'Plates', MODE_PLATE,     'Similar to VocalEcho with different delay taps'),
    # Live Sound
    'Live Plate (731)':      ('Live Plate',       'Plates', MODE_PLATE,     'Crisp clean basic plate, medium decay'),
    'Clean Plate (715)':     ('Clean Plate',      'Plates', MODE_PLATE,     'Clean plate with diffusion control'),
    'Live Gate (716)':       ('Live Gate',        'Plates', MODE_PLATE,     'Tight gate or crisp inverse sounds on the fly'),
    'Bright Plate (712)':    ('Bright Plate',     'Plates', MODE_PLATE,     'Small bright plate, short decay, enhancing'),
    'Hot Plate (714)':       ('Hot Plate',        'Plates', MODE_PLATE,     'Medium sizzling plate, optimized for live mixing'),
    'Ever Plate (717)':      ('Ever Plate',       'Plates', MODE_PLATE,     'Mono level patched to Attack and Spread'),
    'Warm Plate (718)':      ('Warm Plate',       'Plates', MODE_PLATE,     'Slightly warmer plate with less edge'),
    'Live Drums (720)':      ('Live Drums Plate', 'Plates', MODE_PLATE,     'Medium plate, short reverb time for full kit'),
    'Great Plate (721)':     ('Great Plate',      'Plates', MODE_PLATE,     'Basic plate, not too dark and not too bright'),
    # Drums&Perc
    'Big Drums (713)':       ('Big Drums',        'Plates', MODE_PLATE,     'Medium size plate, high diffusion, moderate decay'),
    'Drum Plate (722)':      ('Drum Plate',       'Plates', MODE_PLATE,     'Large dark plate, high diffusion, long decay'),
    'Fat Drumz (723)':       ('Fat Drums',        'Plates', MODE_PLATE,     'Moderate sized, deep sounding plate, high attack'),
    'Cool Plate (724)':      ('Cool Plate',       'Plates', MODE_PLATE,     'Short dull plate for percussion'),
    'Tight Plate (725)':     ('Tight Plate',      'Plates', MODE_PLATE,     'Small and tight, moderate diffusion, for percussion'),
    'Short Plate (752)':     ('Short Plate',      'Plates', MODE_PLATE,     'Short plate reverb, fairly short decay, good high end'),
    'Dark Plate (709)':      ('Dark Plate',       'Plates', MODE_PLATE,     'Classic! Dark, smooth, long decay, fatten percussion'),
    'Plate Gate (754)':      ('Plate Gate',       'Plates', MODE_PLATE,     'Gate with tonal qualities of a plate'),
    'Plate Gate 2 (753)':    ('Plate Gate 2',     'Plates', MODE_PLATE,     'Heavy, dense, short, nonlinear reverb'),
    'BongoPlate (744)':      ('Bongo Plate',      'Plates', MODE_PLATE,     'Gives bongos and native drums thickness'),
    # Custom
    'Plate 90 (745)':        ('Plate 90',         'Plates', MODE_PLATE,     'General purpose, dark plate'),
    'WhatTheHeck (726)':     ('What The Heck',    'Plates', MODE_PLATE,     'Tap tempo-controlled LFO1 modulates High Cut'),
    'GtrDlyPlate (746)':     ('Guitar Dly Plate', 'Plates', MODE_PLATE,     'Basic guitar delay with plate reverb mixed in'),
    'MonoOrStereo (749)':    ('Mono Or Stereo',   'Plates', MODE_PLATE,     'General plate, run in mono, stereo, or 3 choices'),
    'SpatialPlate (750)':    ('Spatial Plate',    'Plates', MODE_PLATE,     'Plate reverb with two LFOs controlling InWidth/OutWidth'),
    'Gen. Plate (751)':      ('Gen. Plate',       'Plates', MODE_PLATE,     'Generic plate preset, starting place'),
    'Patterns (747)':        ('Patterns',         'Plates', MODE_PLATE,     'Tempo-driven spatial effect for dramatic spatial effects'),
    'MultiPlateDly (748)':   ('Multi Plate Dly',  'Plates', MODE_PLATE,     'Multi-purpose plate delay with custom controls'),
}

# ============================================================================
# PCM 90 P3: Post — Mode assignments
# ============================================================================
POST_MAP = {
    # Indoor Small
    'Cabin Fever (774)':     ('Cabin Fever',      'Rooms',    MODE_ROOM,      'Sounds like snowed in too long'),
    'Echo_Kitchen (775)':    ('Echo Kitchen',     'Rooms',    MODE_ROOM,      'Syncopated echo delay inside small kitchen'),
    'HardwoodRoom (784)':    ('Hardwood Room',    'Rooms',    MODE_ROOM,      'Designed to sound like hardwood floor room'),
    'MeetingRoom (796)':     ('Meeting Room',     'Rooms',    MODE_ROOM,      'Hotel-like meeting room'),
    'Locker Room (759)':     ('Locker Room',      'Rooms',    MODE_ROOM,      'Ambience of a locker room'),
    'Living Room (785)':     ('Living Room',      'Rooms',    MODE_ROOM,      'Soft room with short RT, some stereo width'),
    'Bedroom (776)':         ('Bedroom',          'Rooms',    MODE_ROOM,      'Small bedroom with furniture and heavy curtains'),
    'Dual Closets (777)':    ('Dual Closets',     'Rooms',    MODE_ROOM,      'Split effect, empty and full closet'),
    'Phone Booth (778)':     ('Phone Booth',      'Rooms',    MODE_ROOM,      'How much sound can you squeeze into a phone booth?'),
    'Coffin (779)':          ('Coffin',           'Rooms',    MODE_ROOM,      'Tight small space, open or closed casket'),
    # Indoor Large
    'MetalChamber (780)':    ('Metal Chamber',    'Halls',    MODE_CHAMBER,   'Short, boomny, and bright, like anechoic chamber'),
    'Stairwell (781)':       ('Stairwell',        'Halls',    MODE_ROOM,      'Short decay of single room, large reflections'),
    'Make-A-Space (783)':    ('Make-A-Space',     'Halls',    MODE_ROOM,      'Liveness controls let you design your room'),
    'Dly_Hallway (787)':     ('Delay Hallway',    'Halls',    MODE_HALL,      'Split, short ping-pong delay, medium-long hallway'),
    'LectureHalls (786)':    ('Lecture Halls',    'Halls',    MODE_HALL,       'Split with empty and full hall'),
    'Dance Hall (799)':      ('Dance Hall',       'Halls',    MODE_HALL,       'Medium bright hall'),
    'Ballrooms (789)':       ('Ballrooms',        'Halls',    MODE_HALL,       'Two different shaped ballrooms'),
    'Empty Club (790)':      ('Empty Club',       'Halls',    MODE_ROOM,      'Typical Monday night at the club'),
    'NYC Clubs (791)':       ('NYC Clubs',        'Halls',    MODE_ROOM,      'Acoustics of two famous NYC nightclubs'),
    'Sports Verbs (800)':    ('Sports Verbs',     'Halls',    MODE_HALL,       'Split reverb with locker room and arena'),
    # Outdoor
    'Inside-Out (792)':      ('Inside-Out',       'Creative', MODE_HALL,       'Strange hall with input level controlling width'),
    'Outdoor PA (793)':      ('Outdoor PA',       'Creative', MODE_ROOM,      'Open space, not much reflection, max DryDly'),
    'Outdoor PA-2 (794)':    ('Outdoor PA 2',     'Creative', MODE_ROOM,      'Similar to Outdoor PA, 5 different settings'),
    'Two Autos (782)':       ('Two Autos',        'Creative', MODE_ROOM,      'Inside of a VW van and inside of a VW bug'),
    'NYC Tunnels (772)':     ('NYC Tunnels',      'Creative', MODE_HALL,       'Split simulating two automobile tunnels'),
    'Indoors_Out (773)':     ('Indoors/Out',      'Creative', MODE_CHAMBER,   'Medium chamber and an outdoor space'),
    'Echo Beach (797)':      ('Echo Beach',       'Creative', MODE_HALL,       'Echo, echo, echo. Master delays and outdoor echo'),
    'Block Party (788)':     ('Block Party',      'Creative', MODE_ROOM,      'Input signals reflect off brick buildings'),
    'Stadium (756)':         ('Stadium',          'Creative', MODE_HALL,       'Designed to simulate a large sports stadium'),
    'Dull_Bright (757)':     ('Dull/Bright',      'Creative', MODE_HALL,       'Dull backstage sound and large open space'),
    # Spatial
    'Wobble Room (758)':     ('Wobble Room',      'Creative', MODE_CHORUS_SPACE, 'LFO drives OutWidth to make the room wobble'),
    'Spatializer (760)':     ('Spatializer',      'Creative', MODE_CHORUS_SPACE, 'Compress and Expand ratios are cranked'),
    'Mic Location (795)':    ('Mic Location',     'Creative', MODE_ROOM,       'Bipolar ADJUST to add Predelay or Dry Delay'),
    'Voices_ (761)':         ('Voices?',          'Creative', MODE_RANDOM_SPACE, 'Get lost in the crowd, produces multiple voices'),
    'Voices_ 2 (762)':       ('Voices? 2',        'Creative', MODE_RANDOM_SPACE, 'Similar to Voices?, with LFO controlling OutWidth'),
    'Window (763)':          ('Window',           'Creative', MODE_ROOM,       'Opposite side of windows that can be opened'),
    'Wall Slap (764)':       ('Wall Slap',        'Creative', MODE_ROOM,       'Decay level, predelay, dry delay, dry mix'),
    'BombayClub (765)':      ('Bombay Club',      'Creative', MODE_ROOM,       'Varies Decay, Out Width, and High Cut'),
    # Custom
    'X Variable (766)':      ('X Variable',       'Creative', MODE_HALL,       'Custom Controls for variable equation'),
    'Y Variable (767)':      ('Y Variable',       'Creative', MODE_HALL,       'Random Hall version of X Variable'),
    'Sound Check (768)':     ('Sound Check',      'Creative', MODE_HALL,       'Imagine an empty hall from the perspective of stage'),
    'Sound Stage (769)':     ('Sound Stage',      'Creative', MODE_HALL,       'Changes Pre Delay/Dry Delay mix'),
    'Reverse Taps (801)':    ('Reverse Taps',     'Creative', MODE_HALL,       'Tempo reflects Dry L/R from 0.292-32.49 sec'),
    'Air Pressure (770)':    ('Air Pressure',     'Creative', MODE_ROOM,       'Adjust compression/expansion and Custom 1'),
    'The Tomb (771)':        ('The Tomb',         'Creative', MODE_CATHEDRAL,  'Places source within a very reflective tomb'),
    'Mr. Vader (755)':       ('Mr. Vader',        'Creative', MODE_DIRTY_HALL, 'Select Buzzing or Modulated special effects'),
    'Mythology (798)':       ('Mythology',        'Creative', MODE_HALL,       'Size and Delay inversely proportionate, supernatural'),
}


def get_all_ir_mappings() -> list[dict]:
    """
    Scan all IR directories and return mappings for every IR that exists.

    Returns list of dicts with keys:
        ir_path, preset_name, category, mode, mode_name, description, pcm90_bank
    """
    all_maps = [
        ('Halls', HALLS_MAP),
        ('Rooms', ROOMS_MAP),
        ('Plates', PLATES_MAP),
        ('Post', POST_MAP),
    ]

    results = []
    for pcm90_bank, mapping in all_maps:
        ir_dir = os.path.join(PCM90_IR_BASE, pcm90_bank)
        if not os.path.isdir(ir_dir):
            continue

        # List all IR files in directory
        ir_files = [f for f in os.listdir(ir_dir) if f.endswith('.wav')]

        for ir_file in sorted(ir_files):
            # Extract the key part: "pcm 90, <name>_dc.wav" -> "<name>"
            name_part = ir_file.replace('pcm 90, ', '').replace('_dc.wav', '')

            # Try to find in mapping
            matched = False
            for key, (preset_name, category, mode, description) in mapping.items():
                if key == name_part:
                    results.append({
                        'ir_path': os.path.join(ir_dir, ir_file),
                        'ir_file': ir_file,
                        'preset_name': preset_name,
                        'category': category,
                        'mode': mode,
                        'mode_name': MODE_NAMES[mode],
                        'description': description,
                        'pcm90_bank': pcm90_bank,
                    })
                    matched = True
                    break

            if not matched:
                # If not in our mapping, still include it with a best-guess
                clean_name = name_part.split('(')[0].strip()
                # Guess mode from bank
                default_mode = {
                    'Halls': MODE_HALL, 'Rooms': MODE_ROOM,
                    'Plates': MODE_PLATE, 'Post': MODE_ROOM,
                }[pcm90_bank]
                default_cat = 'Creative' if pcm90_bank == 'Post' else pcm90_bank
                results.append({
                    'ir_path': os.path.join(ir_dir, ir_file),
                    'ir_file': ir_file,
                    'preset_name': clean_name,
                    'category': default_cat,
                    'mode': default_mode,
                    'mode_name': MODE_NAMES[default_mode],
                    'description': f'(auto-mapped from {pcm90_bank})',
                    'pcm90_bank': pcm90_bank,
                })

    return results


def print_mapping_summary():
    """Print summary of all mapped IRs."""
    mappings = get_all_ir_mappings()
    print(f"Total IRs mapped: {len(mappings)}")
    print()

    by_cat = {}
    for m in mappings:
        cat = m['category']
        by_cat.setdefault(cat, []).append(m)

    for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
        items = by_cat.get(cat, [])
        print(f"\n{'='*60}")
        print(f"  {cat}: {len(items)} presets")
        print(f"{'='*60}")
        for m in items:
            print(f"  {m['preset_name']:25s}  {m['mode_name']:15s}  ({m['pcm90_bank']})")


if __name__ == '__main__':
    print_mapping_summary()
