"""Bind reusable S1 object assets to their actual v3 data and placements.

Unchanged payloads are located by complete byte identity against the owner's
retail S1 input. Changed payloads are selected by verified v3 PLC entries or
object initialization instructions, rather than retail absolute offsets.
"""
import struct
from s1_objects import (S1Objects,ASSETS,ASSET_EXTRA_ART,ZONE_NEED,BOSS_NEED,
                        load_symbols,obj_assets,anim_blob,be16,RING_POS,
                        ASSET_NAME,RESULTS_VISIBLE,RESULTS_TILES,asset_frames,
                        art_source,_piece_ranges_lines,_find_run,asset_mappings,ASSET_REC)
from mdcodecs import nemesis_decode

CHANGED_ART={
    'Nem_Buzz':(4,0x8880),'Nem_Chopper':(4,0x8F60),
    'Nem_Motobug':(4,0x9E00),'Nem_Eggman':(0x11,0x8000),
    'Nem_SignPost':(1,0xD000),'Nem_Bumper':(0xD,0x7000),
    'Nem_Roller':(0xC,0x9700),'Nem_Water':(0x28,0x6000),
    'Nem_Burrobot':(6,0x94C0),'Nem_Orbinaut':(7,0x8CE0),
    'Nem_Bomb':(0xA,0x8000),'Nem_Electric':(0xF,0x8FC0),
}
CHANGED_MAPS={'Map_Buzz':(0xE1E2,0xE42E),'Map_Burro':(0xFFE6,0x1017C),
              'Map_Surf':(0x16B08,0x16BB0)}
ZONE_NAMES=('GHZ','LZ','MZ','SLZ','SYZ','SBZ')
# Megamix monitor item IDs are converted separately from the retail S1 set.
# Exact contents are bound by the v3 monitor's item-dispatch table.

def relocated_symbols(donor,retail):
    original=load_symbols(True);result=dict(original);labels={}
    for _,_,nem,_,_,maps,anim,_ in ASSETS:
        for kind,name in (('nem',nem),('maps',maps),('ani',anim)):
            if name:labels[name.removesuffix('*2')]=kind
    for extras in ASSET_EXTRA_ART.values():
        for _,name in extras:
            if name!='LEVEL':labels[name.removesuffix('*2')]='nem'
    for name,kind in labels.items():
        if name=='Map_SStom':
            if donor.blob(0x10D46,8)!=bytes.fromhex('237c00010fc20004'):
                raise ValueError('Unproven Megamix sideways stomper mapping')
            result[name]=0x10FC2;continue
        if name in CHANGED_ART:
            plc,dst=CHANGED_ART[name]
            matches=[src for src,vram in donor.plc(plc) if vram==dst]
            if len(matches)!=1:raise ValueError(f'Unproven Megamix PLC asset {name}')
            result[name]=matches[0];continue
        if name in CHANGED_MAPS:
            pc,address=CHANGED_MAPS[name]
            expected=struct.pack('>HIH',0x217C,address,4)
            if donor.blob(pc,8)!=expected:raise ValueError(f'Unproven Megamix object mapping {name}')
            result[name]=address;continue
        address=original[name]
        if kind=='nem':
            _,end=nemesis_decode(retail,address);payload=retail[address:end]
        elif kind=='maps':
            count=be16(retail,address)//2
            end=max(address+be16(retail,address+i*2)+1+
                    retail[address+be16(retail,address+i*2)]*5 for i in range(count))
            payload=retail[address:end]
        else:payload=anim_blob(retail,address)
        found=donor.data.find(payload)
        if found<0:raise ValueError(f'Megamix asset needs a verified binding: {name}')
        result[name]=found
    return result

class MegamixObjects(S1Objects):
    def __init__(self,donor,retail,tables,limit):
        self.donor=donor;self.rom=donor.data
        self.sym=relocated_symbols(donor,retail);self.tables=tables;self.limit=limit
        self.sprite_states={}

    def _place(self,bank,pending,decoded,free,zone_name):
        # Match complete contiguous pieces, including pieces contained in a
        # previous group. Different palettes retain separate pixel encodings.
        # Cache lifetime is one act, including its deferred boss upload pass.
        free=self.virtual_free
        groups={};keep=set()
        for aid,nem,maps,_,line in pending:
            for first,n,relative_line in _piece_ranges_lines(maps,asset_frames(ASSET_NAME[aid],zone_name)):
                source,start=art_source(ASSET_NAME[aid],nem,first)
                if source=='LEVEL':continue
                key=(source,(line+relative_line)&3)
                groups.setdefault(key,set()).add((start,start+n))
                if ASSET_NAME[aid] in RESULTS_VISIBLE:keep.add(key)
        order=[]
        for key,spans in groups.items():
            merged=[]
            for lo,hi in sorted(spans):
                if merged and lo<merged[-1][1]:merged[-1][1]=max(hi,merged[-1][1])
                else:merged.append([lo,hi])
            order.extend((hi-lo,key,lo) for lo,hi in merged)
        place={};segments={}
        for n,key,start in sorted(order,key=lambda v:(0 if v[1] in keep else 1,-v[0])):
            source,line=key;raw=decoded[source]
            if start+n>len(raw)//32:
                raise ValueError(f'{zone_name} asset {source} references missing tile run {start}+{n}')
            tab=list(range(16)) if line==0 else self.tables[line]
            pixels=bytes((tab[b>>4]<<4)|tab[b&15] for b in raw[start*32:(start+n)*32])
            ext=line==0;t=None
            for old,base,old_ext in self.shared_pieces:
                if old_ext!=ext:continue
                offset=old.find(pixels)
                while offset>=0 and offset%32:offset=old.find(pixels,offset+1)
                if offset>=0:
                    candidate=base+offset//32
                    if key not in keep or not (set(range(candidate,candidate+n))&RESULTS_TILES):
                        t=candidate;break
            if t is None:
                t=_find_run(free-RESULTS_TILES,n) if key in keep else _find_run(free,n)
                if t is None:raise ValueError(f'{zone_name}: no VRAM for {key} group {start}+{n}; free={len(free)}')
                used=set(range(t,t+n));free-=used
                self.shared_pieces=[p for p in self.shared_pieces
                    if not (set(range(p[1],p[1]+len(p[0])//32))&used)]
                self.shared_pieces.append((pixels,t,ext))
                segments.setdefault(source,[])
            else:
                # Each asset's independent upload remains complete, including
                # deferred art and a later title-card/VRAM restoration.
                segments.setdefault(source,[])
            for i in range(n):place[(key,start+i)]=t+i
            self.sprite_pixels[t*32:(t+n)*32]=pixels
            self.sprite_lines[t:t+n]=bytes([int(ext)])*n
        return place,segments

    def build_assets(self,bank,zone_name,free_level,need=None,level_slot=None,act=None):
        state=self.sprite_states.setdefault(zone_name,dict(pieces=[],free=set(range(1,2048)),
                                                           pixels=bytearray(65536),lines=bytearray(2048)))
        self.shared_pieces=state['pieces'];self.virtual_free=state['free']
        self.sprite_pixels=state['pixels'];self.sprite_lines=state['lines']
        pointer=super().build_assets(bank,zone_name,free_level,need,level_slot,act)
        for aid,name,nem,tile,line,maps,_,zones in ASSETS:
            if not nem or zone_name not in zones.split(',') or (need is not None and name not in need):continue
            entry=pointer-0x400000+aid*ASSET_REC
            mapping=struct.unpack_from('>I',bank.data,entry)[0]-0x400000
            if mapping<0:continue
            source=asset_mappings(self.rom,self.sym,name,maps,tile,line)
            for frame in range(be16(source,0)//2):
                original=be16(source,frame*2);native=be16(bank.data,mapping+frame*2)+mapping
                for i in range(be16(source,original)):
                    p=original+2+i*6;first=be16(source,p+2)&0x7FF
                    art,_=art_source(name,nem,first)
                    if art!='LEVEL':bank.data[native+3+i*6]|=0x80
            count=be16(bank.data,entry+6)
            struct.pack_into('>H',bank.data,entry+6,count|0x4000)
        self.sprite_tiles=2048-len(self.virtual_free)
        self.sprite_tiles=max((i for i in range(2048) if i not in self.virtual_free),default=0)+1
        state['pieces']=self.shared_pieces
        return pointer

    def act_assets(self,zone,act):
        name=ZONE_NAMES[zone];need=set()
        for item in self.donor.objects(zone,act):
            need.update(obj_assets(item['id'],item['subtype'],name))
        if act==2 or (zone==5 and act==1):
            need.update(BOSS_NEED.get((name,act),BOSS_NEED.get(name,[])))
        need.update(ZONE_NEED.get(name,[]))
        return need

    def asset_max_x(self,zone,act):
        name=ZONE_NAMES[zone];result={}
        for item in self.donor.objects(zone,act):
            for asset in obj_assets(item['id'],item['subtype'],name):
                result[asset]=max(result.get(asset,0),item['x'])
        return result

    def convert_objects(self,zone,act):
        objects=[];rings=[]
        for item in self.donor.objects(zone,act):
            x,y,oid,sub=item['x'],item['y'],item['id'],item['subtype']
            if not oid:continue
            if oid==0x25:
                dx,dy=RING_POS[sub>>4]
                for i in range(min(sub&7,6)+1):rings.append(((x+dx*i)&65535,(y+dy*i)&65535))
                continue
            # Keep all donor item subtypes. A game-owned Megamix monitor
            # adapter handles these; retail S1's conversion loses elements.
            if oid==0x4B:sub=sum(o[2]==0x4B for o in objects)
            original_sub=sub
            if oid==0x26:
                sub={0:5,1:2,2:1,3:4,4:7,5:8,6:3,7:6,8:7}.get(sub,0)
            flips=(item['flags']>>14)&3
            objects.append((x,y|(flips<<13)|0x8000,oid,sub,original_sub if oid==0x26 else 255))
        objects.sort();rings.sort()
        self.monitor_tags=bytes(o[4] for o in objects)
        ob=b''.join(struct.pack('>HHBB',*o[:4]) for o in objects)+b'\xFF\xFF\0\0\0\0'
        rb=b'\0\0\0\0'+b''.join(struct.pack('>HH',*r) for r in rings)+b'\xFF\xFF\0\0'
        return b'\xFF\xFF\0\0\0\0'+ob,rb
