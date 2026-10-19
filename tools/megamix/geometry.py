"""Convert verified v3 geometry into S3K layout/chunk/collision structures.

This covers static geometry. Object assets, animated art, parallax, water and
stage events need separate runtime integration and validation.
"""
import struct

from .v3_rom import TABLES


def convert_geometry(rom,zone,act):
    # Imports resolve through tools/trilogy, set by the calling converter.
    from mdcodecs import enigma_decode,kosinski_decode
    from build_bank import s3k_layout,flip_shape
    info=rom.stage(zone,act);header=info['header']
    blocks=bytearray(enigma_decode(rom.data,header['blocks']))
    source_blocks=bytes(blocks[:len(blocks)//8*8])
    source_count=len(source_blocks)//8
    source_chunks,_=kosinski_decode(rom.data,header['chunks'])
    collision=rom.blob(info['collision_index'],source_count)
    angles=bytearray(rom.blob(TABLES['angles'],256))
    heights=bytearray(rom.blob(TABLES['heights'],4096))
    rotated=bytearray(rom.blob(TABLES['rotated_heights'],4096))
    # Keep a guaranteed empty native block/chunk zero. A source block zero
    # is still a real block when an occupied source cell references it.
    blocks=bytearray(8)
    primary=bytearray((0,));secondary=bytearray(primary)
    cells={};canonical={(bytes(8),0,0):0};first_source={};synthetic=set();mirrored={}
    free=[i for i in range(1,256) if i not in set(collision)]
    chunks=[bytes(128)];chunk_index={chunks[0]:0}
    checks=[]
    equivalent_words={};word_cache={};shape_cache={}

    def oriented_visual(data,flips):
        t=list(struct.unpack('>4H',data))
        if flips&1:t=[t[1]^0x800,t[0]^0x800,t[3]^0x800,t[2]^0x800]
        if flips&2:t=[t[2]^0x1000,t[3]^0x1000,t[0]^0x1000,t[1]^0x1000]
        return tuple(t)

    def oriented_shape(index,flips):
        if not index:return None
        key=(index,flips)
        if key not in shape_cache:
            if flips:
                normal,turn,angle=flip_shape(heights,angles,index,(flips&1,(flips>>1)&1))
                shape_cache[key]=(bytes(normal),bytes(turn),angle)
            else:
                shape_cache[key]=(bytes(heights[index*16:(index+1)*16]),
                                  bytes(rotated[index*16:(index+1)*16]),angles[index])
        return shape_cache[key]

    def word(p):return int.from_bytes(source_chunks[p:p+2],'big')

    def cell(front,back,background=False):
        cached=word_cache.get((front,back,background))
        if cached is not None:
            checks.append((front,back,cached,background));return cached
        source=front&0x7FF;other=back&0x7FF
        if source>=source_count or other>=source_count:
            raise ValueError('Megamix chunk references a missing block')
        flips=((front^back)>>11)&3
        key=(source,other,flips)
        if background and source in first_source:
            cells[key]=first_source[source]
        if key not in cells:
            shape=collision[other]
            if source!=other or flips:synthetic.add(key)
            if flips and shape:
                key2=(shape,flips)
                if key2 not in mirrored:
                    if not free:raise ValueError('No free collision shape for mirrored loop path')
                    index=free.pop(0)
                    normal,turn,angle=flip_shape(heights,angles,shape,(flips&1,(flips>>1)&1))
                    heights[index*16:(index+1)*16]=normal
                    rotated[index*16:(index+1)*16]=turn;angles[index]=angle
                    mirrored[key2]=index
                shape=mirrored[key2]
            visual=source_blocks[source*8:(source+1)*8]
            equivalent=(visual,collision[source],shape)
            if equivalent not in canonical:
                canonical[equivalent]=len(blocks)//8
                blocks.extend(visual);primary.append(collision[source]);secondary.append(shape)
            cells[key]=canonical[equivalent]
        native=cells[key]
        first_source.setdefault(source,native)
        if native>=768:raise ValueError(f'{info["name"]} exceeds native collision-block capacity')
        value=(native | (((front>>11)&3)<<10)
               | (((front>>13)&3)<<12) | (((back>>13)&3)<<14))
        if not native:value=0 # blank visuals and both collision shapes are zero
        # Equal oriented visuals AND collision shapes/angles can use the
        # same native word even when source blocks encode different flips.
        # Keep both paths' solidity flags in the key.
        physical=(oriented_visual(source_blocks[source*8:(source+1)*8],(front>>11)&3),
                  oriented_shape(collision[source],(front>>11)&3),
                  oriented_shape(collision[other],(back>>11)&3),
                  (front>>13)&3,(back>>13)&3)
        if not background:value=equivalent_words.setdefault(physical,value)
        word_cache[(front,back,background)]=value
        checks.append((front,back,value,background))
        return value

    def quad(source,qx,qy,other=None,background=False):
        result=[]
        for row in range(8):
            for col in range(8):
                offset=((qy*8+row)*16+qx*8+col)*2
                front=word(source*512+offset)
                back=word(other*512+offset) if other is not None else front
                result.append(cell(front,back,background))
        data=struct.pack('>64H',*result)
        if data not in chunk_index:
            if len(chunks)>=65536:raise ValueError('Megamix act exceeds extended chunk count')
            chunk_index[data]=len(chunks);chunks.append(data)
        return chunk_index[data]

    def layout(plane):
        _,rows=rom.layout(zone,act,plane);result=[]
        for row in rows:
            top=[];bottom=[]
            for value in row:
                if not value:
                    top.extend((0,0));bottom.extend((0,0));continue
                index=(value&0x7F)-1;other=None
                # v3 Sonic_Loops ($1A746) runs only in source zones 0/3.
                # Other zones never select a behind-loop collision path;
                # manufacturing paired chunks there wastes native capacity.
                if value&0x80 and plane==0 and zone in (0,3):
                    other=(value&0x7F)+1
                    if other==0x29:other=0x51
                    other-=1
                ids=[quad(index,x,y,other,plane==1) for y in range(2) for x in range(2)]
                top.extend(ids[:2]);bottom.extend(ids[2:])
            result.extend((top,bottom))
        return result

    front=layout(0);foreground_chunks=len(chunks)
    back=layout(1)
    solids=bytearray(0xC00)
    for i in range(len(primary)):
        solids[i*2]=primary[i];solids[0x600+i*2]=secondary[i]
    # Round-trip assertions compare original cell visuals and both solidity
    # flags, not merely whether a generated pointer exists.
    for original,behind,native,background in checks:
        index=native&0x3FF
        source=original&0x7FF
        if oriented_visual(blocks[index*8:(index+1)*8],(native>>10)&3) != oriented_visual(source_blocks[source*8:(source+1)*8],(original>>11)&3):
            raise ValueError('Converted cell changed foreground block visuals')
        if oriented_shape(primary[index],(native>>10)&3)!=oriented_shape(collision[source],(original>>11)&3):
            raise ValueError('Converted primary collision shape mismatch')
        if not index:continue # flags/flips on a completely empty cell have no effect
        if not background and oriented_shape(secondary[index],(native>>10)&3)!=oriented_shape(collision[behind&0x7FF],(behind>>11)&3):
            raise ValueError('Converted secondary collision shape mismatch')
        if ((native>>12)&3)!=((original>>13)&3):raise ValueError('Primary solidity mismatch')
        if ((native>>14)&3)!=((behind>>13)&3):raise ValueError('Secondary solidity mismatch')
    # Preserve every chunk in a separate 16-bit representation when the
    # retail 8-bit layout cannot represent this act. Runtime support is
    # required before such a payload can be enrolled; never truncate IDs.
    layout16=struct.pack('>4s4H',b'MGL1',len(front[0]),len(back[0]),len(front),len(back))
    for plane in (front,back):
        for row in plane:layout16+=struct.pack('>'+str(len(row))+'H',*row)
    native_layout=s3k_layout(front,back) if len(chunks)<=256 else b''
    return dict(layout=native_layout,layout16=layout16,chunks=b''.join(chunks),blocks=bytes(blocks),
                solids=bytes(solids),angles=bytes(angles),heights=bytes(heights),rotated=bytes(rotated),
                checked_cells=len(checks),source_blocks=source_count,
                native_blocks=len(blocks)//8,native_chunks=len(chunks),
                foreground_chunks=foreground_chunks,
                synthetic_blocks=len(synthetic),mirrored_shapes=len(mirrored),
                requires_extended_chunks=len(chunks)>256)
