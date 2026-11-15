/* Smooth act-map handoffs change only rendered framing.
 * Native coordinates, activation, input and level-event state are untouched. */
typedef struct {
    int valid,zone,act,width,map_width,desired,value,error,age,active;
    unsigned tick,event;
    int alpha,fade_in;
} CameraHandoffPreview;
static CameraHandoffPreview s_handoff;
static int handoff_mode(void)
{
    static int mode=-1;
    if(mode<0) {
        const char *s=getenv("SONIC3_CAMERA_HANDOFF");mode=1;
        if(s && !strcmp(s,"baseline"))mode=0;
        if(s && !strcmp(s,"ease"))mode=1;
        if(s && !strcmp(s,"anticipate"))mode=2;
        if(s && !strcmp(s,"dip"))mode=3;
    }
    return mode;
}
/* Secondary art loads reuse banks outside the native 320px viewport.
 * Retain the latest complete outgoing world/art through that reload, and
 * while a glide still reveals the previous side of the coordinate rebase. */
typedef struct {
    int valid,zone;
    unsigned tick;
    uint8_t world[0x10000];
    uint32_t colors[224][64];
    GVDP video;
} CameraHandoffArt;
static CameraHandoffArt s_handoff_art;
static int s_handoff_palette_capture;
static void handoff_art_capture(const GVDP *v,const uint8_t *world,int camera,
                                unsigned zone,unsigned act,unsigned event)
{
    if(s_interpolating)return;
    s_handoff_palette_capture=0;
    unsigned tick=((unsigned)world[0xFE04]<<8)|world[0xFE05];
    if(s_handoff_art.valid && s_handoff_art.zone!=(int)zone)s_handoff_art.valid=0;
    int late=(zone==1 && camera>=0x3200) || (zone==2 && camera>=0x2800) ||
        (zone==5 && camera>=0x6500);
    unsigned normal=zone==5?16u:0u;
    if(!act && late && event==normal &&
       (!s_handoff_art.valid || s_handoff_art.tick!=tick)) {
        memcpy(s_handoff_art.world,world,sizeof s_handoff_art.world);
        memcpy(s_handoff_art.video.vram,v->vram,sizeof v->vram);
        memcpy(s_handoff_art.video.vsram,v->vsram,sizeof v->vsram);
        memcpy(s_handoff_art.video.reg,v->reg,sizeof v->reg);
        s_handoff_art.zone=(int)zone;s_handoff_art.valid=1;s_handoff_art.tick=tick;
        s_handoff_palette_capture=1;
    }
}
static int handoff_art_loading(unsigned zone,unsigned act,unsigned event)
{
    return s_handoff_art.valid && s_handoff_art.zone==(int)zone && !act &&
        ((zone==1 || zone==2)?event==4:zone==5 && event==20);
}
static int handoff_art_sample(unsigned zone,unsigned act,int x,int y,
                              uint16_t *attr,uint8_t *pixel)
{
    if(!s_handoff_art.valid || s_handoff_art.zone!=(int)zone || act!=1 || x>=0)return 0;
    int dx=zone==1?0x3600:zone==2?0x2E00:0x6880;
    int dy=zone==2?0x600:zone==5?-0x100:0;
    *attr=world_attr(s_handoff_art.world,x+dx,y+dy,0);
    *pixel=pattern_pixel(&s_handoff_art.video,*attr,x+dx,y+dy);
    return 1;
}
/* The outgoing HCZ bank has transparent foreground over its old backdrop.
 * Retain that backdrop's own installed scroll while the pan still exposes
 * negative incoming X; otherwise the moving-wall bank leaves a black seam. */
static int handoff_art_background(unsigned zone,unsigned act,int wx,int x,int line,
                                  int width,int native_width,uint16_t *attr,uint8_t *pixel)
{
    if(zone!=1 || act!=1 || wx>=0 || !s_handoff_art.valid ||
       s_handoff_art.zone!=(int)zone)return 0;
    const GVDP *v=&s_handoff_art.video;const uint8_t *world=s_handoff_art.world;
    unsigned base=(v->reg[13]&63u)<<10;
    int mode=v->reg[11]&3,row=mode==0?0:mode==2?line&~7:line;
    int scroll=(int16_t)vram16(v,base+(unsigned)row*4+2);
    int bx=x-(width-native_width)/2-scroll;
    int period=(((unsigned)world[0x8002]<<8)|world[0x8003])*128;
    if(period>0){bx%=period;if(bx<0)bx+=period;}
    int camera_y=((unsigned)world[0xEE90]<<8)|world[0xEE91];
    int by=unwrap(v->vsram[1],camera_y)+line;
    *attr=world_attr(world,bx,by,1);*pixel=pattern_pixel(v,*attr,bx,by);
    return 1;
}
static void handoff_art_state(S3StateIO *io)
{
    int valid=s_handoff_art.valid,zone=s_handoff_art.zone;
    unsigned tick=s_handoff_art.tick;
    s3_state_peek(io,0,&valid,sizeof valid);
    s3_state_peek(io,sizeof valid,&zone,sizeof zone);
    s3_state_peek(io,sizeof valid+sizeof zone,&tick,sizeof tick);
    if(valid<0 || valid>1 || zone<0 || zone>255 || tick>65535)io->ok=0;
    S3_STATE(io,s_handoff_art.valid);S3_STATE(io,s_handoff_art.zone);
    S3_STATE(io,s_handoff_art.tick);
    S3_STATE(io,s_handoff_art.world);S3_STATE(io,s_handoff_art.video.vram);
    S3_STATE(io,s_handoff_art.video.vsram);S3_STATE(io,s_handoff_art.video.reg);
    S3_STATE(io,s_handoff_art.colors);
    if(io->mode==2)s_handoff_palette_capture=0;
}

static int handoff_origin(int camera,int desired,int width,int map_width,
                          unsigned zone,unsigned act,unsigned event,
                          unsigned tick,int paused,int level)
{
    int mode=handoff_mode();
    if(!mode)return desired;
    int eligible=level && width>320 && (zone==1 || zone==2 || zone==5) && act<=1;
    if(!eligible){s_handoff.valid=0;s_handoff.alpha=0;return desired;}
    if(s_interpolating)return s_handoff.valid && s_handoff.active?s_handoff.value:desired;
    if(!s_handoff.valid || s_handoff.zone!=(int)zone || s_handoff.width!=width) {
        s_handoff=(CameraHandoffPreview){0};s_handoff.valid=1;
        s_handoff.zone=(int)zone;s_handoff.act=(int)act;s_handoff.width=width;
        s_handoff.map_width=map_width;s_handoff.desired=s_handoff.value=desired;
        s_handoff.event=event;s_handoff.tick=tick;return desired;
    }
    if(paused || tick==s_handoff.tick)return s_handoff.active?s_handoff.value:desired;
    int changed=s_handoff.act!=(int)act || s_handoff.map_width!=map_width;
    int jump=desired-s_handoff.desired;
    if(jump>24 || jump<-24)changed=1;
    int near_end=(!act && ((zone==1 && camera>=0x3200) ||
        (zone==2 && camera>=0x2800) || (zone==5 && camera>=0x6800)));
    int pending=near_end && event && event!=s_handoff.event;
    int loading=near_end && event==(zone==5?20u:4u);
    int incoming=!s_handoff.act && act==1 &&
        s_handoff.event==(zone==5?20u:4u);
    if(changed && (s_handoff.active || loading || incoming)) {
        s_handoff.error=s_handoff.value-desired;s_handoff.age=0;
        s_handoff.active=1;
        if(mode==3){s_handoff.alpha=255;s_handoff.fade_in=1;}
    }
    if(mode==1) {
        if(s_handoff.active) {
            int t=s_handoff.age<45?s_handoff.age:45;
            /* Cubic smoothstep: zero velocity at either end of the pan. */
            int remain=45*45*45-(3*t*t*45-2*t*t*t);
            int offset=(int)((long long)s_handoff.error*remain/(45*45*45));
            s_handoff.value=desired+offset;
            if(++s_handoff.age>45)s_handoff.active=0;
        } else s_handoff.value=desired;
    } else if(mode==2) {
        int target=desired;
        if(pending || (near_end && s_handoff.active && !changed)) {
            int incoming=zone==5?0x80:(width-320)/2;
            /* Never pan beyond the currently owned map merely to anticipate
             * a future one. Fall back to the glide while art is unavailable. */
            int low=camera-map_width+width;
            if(incoming<low)incoming=low;
            if(incoming>camera)incoming=camera;
            target=incoming;s_handoff.active=1;
        }
        int d=target-s_handoff.value;
        if(d>6)d=6;if(d<-6)d=-6;
        s_handoff.value+=d;
        if(!near_end && s_handoff.value==desired)s_handoff.active=0;
    } else {
        s_handoff.value=desired;
        if(pending && !s_handoff.fade_in)s_handoff.alpha=32;
        if(s_handoff.fade_in) {
            if(s_handoff.alpha>0)s_handoff.alpha-=17;
            if(s_handoff.alpha<=0){s_handoff.alpha=0;s_handoff.fade_in=0;}
        } else if(s_handoff.alpha>0 && s_handoff.alpha<255) {
            s_handoff.alpha+=32;if(s_handoff.alpha>255)s_handoff.alpha=255;
        }
    }
    s_handoff.tick=tick;s_handoff.event=event;s_handoff.act=(int)act;
    s_handoff.map_width=map_width;s_handoff.desired=desired;
    return s_handoff.value;
}
static void handoff_dip(uint32_t *out,int width)
{
    if(handoff_mode()!=3 || !s_handoff.alpha)return;
    unsigned light=255u-(unsigned)s_handoff.alpha;
    for(int x=0;x<width;++x) {
        uint32_t c=out[x],v=c&0xFF000000u;
        for(unsigned s=0;s<24;s+=8)v|=(((c>>s)&255u)*light/255u)<<s;
        out[x]=v;
    }
}
static void handoff_state(S3StateIO *io)
{
    int expected=handoff_mode(),stored=expected;
    s3_state_peek(io,0,&stored,sizeof stored);
    CameraHandoffPreview saved=s_handoff;
    s3_state_peek(io,sizeof stored,&saved,sizeof saved);
    if(stored!=expected || saved.valid<0 || saved.valid>1 || saved.zone<0 || saved.zone>255 ||
       saved.act<0 || saved.act>1 || saved.width<0 || saved.width>4096 ||
       saved.map_width<0 || saved.map_width>32768 || saved.age<0 || saved.age>46 ||
       saved.active<0 || saved.active>1 || saved.error<-65536 || saved.error>65536 ||
       saved.desired<-65536 || saved.desired>65536 || saved.value<-65536 || saved.value>65536 ||
       saved.alpha<0 || saved.alpha>255 || saved.fade_in<0 || saved.fade_in>1)io->ok=0;
    S3_STATE(io,stored);S3_STATE(io,s_handoff);
    handoff_art_state(io);
}
