/* Exact dry background bands in v3 Deform_LZ's Megamix branch,
 * $009DEC-$009EF0. Negation and shifts have 68000 word semantics. The
 * low 64 lines use four 16-line bands; all earlier lines scroll at 1/16.
 * Metallic Base 3 takes the original scrolling branch ($9C46), but its
 * background still initializes at $20 in Megamix mode ($98A2).
 */
#include <stdint.h>
static int floor_shift(int value,unsigned bits)
{ return value>=0?value/(1<<bits):-((-value+(1<<bits)-1)/(1<<bits)); }
int tri_megamix_lz_scroll(int camera_x,unsigned line)
{
    int value=(int16_t)(0u-(uint16_t)camera_x);
    if(line<160)return floor_shift(value,4);
    if(line<176)return floor_shift(value,3);
    if(line<192)return floor_shift(value,2)-floor_shift(value,4);
    if(line<208)return floor_shift(value,2);
    return floor_shift(value,1)-floor_shift(value,4);
}

/* Megamix DynamicWater $5882-$5A24. The caller applies the common one-pixel
 * water-speed ramp after this event. These events deliberately differ from
 * Original mode ($5A26 onward): notably act 3 starts at $4C8 without the
 * retail $900/door prerequisite. Return the real act-3 switch event. */
int tri_megamix_water_event(unsigned act,uint16_t x,uint16_t y,
    uint16_t *mean,uint16_t *target,uint8_t *routine)
{
    unsigned d;
    switch(act) {
    case 0:
        if(*routine)return 0;
        d=0x318;
        if(x>=0x500) {
            d=0x108;
            if(y>=0x200) {
                d=0x318;
                if(x>=0x800) {
                    d=y<0x400?0x108:0x418;
                    if(y<0x400 && x>=0xD00)d=0x418;
                    if(x>=0x1600)d=0x108;
                }
            }
        }
        *target=(uint16_t)d;return 0;
    case 1:
        *target=(uint16_t)(x<0x500?0x328:x<0xB00?0x3C8:0x428);return 0;
    case 2:
        switch(*routine) {
        case 0:
            d=x<0x770?0x4C8:0x308;
            if(x>=0x1400 && (*target==0x508 || y>=0x600 || y<0x280)) {
                d=0x508;*mean=(uint16_t)d;
                if(x>=0x1770)*routine=1;
            }
            *target=(uint16_t)d;return 0;
        case 1:
            d=x<0x1860?0x508:0x188;
            if(x>=0x1860 && (x>=0x1AF0 || *mean==d))*routine=2;
            *target=(uint16_t)d;return 0;
        case 2:
            d=x<0x1AF0?0x188:0x900;
            if(x>=0x1BC0) {*routine=3;*target=0x608;*mean=0x7C0;return 1;}
            *target=*mean=(uint16_t)d;return 0;
        default:
            if(x>=0x1E00)*target=0x128;
            return 0;
        }
    default:
        *target=(uint16_t)(x<0xF00?0x228:0x4C8);return 0;
    }
}
