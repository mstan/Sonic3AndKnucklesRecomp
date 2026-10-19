"""Private source-data previews, not native-runtime evidence."""
from PIL import Image,ImageDraw
from .v3_rom import MIGHTY


def colors(raw):
    return [tuple((((int.from_bytes(raw[i:i+2],'big')>>shift)&7)*255+3)//7
                  for shift in (1,5,9)) for i in range(0,len(raw),2)]


def mighty_sheet(rom,path):
    frames=rom.mighty_frames();palette=colors(rom.palette(MIGHTY['palette'])[1])
    cell=80;cols=16;sheet=Image.new('RGB',(cols*cell,((len(frames)+cols-1)//cols)*cell),(42,46,52))
    draw=ImageDraw.Draw(sheet)
    for index,(pieces,tiles) in enumerate(frames):
        origin=(index%cols*cell+cell//2,index//cols*cell+cell//2)
        for p in pieces:
            w,h=p['width']*8,p['height']*8
            for y in range(h):
                for x in range(w):
                    sx=w-1-x if p['x_flip'] else x;sy=h-1-y if p['y_flip'] else y
                    tile=p['tile']+(sx//8)*p['height']+sy//8
                    value=tiles[tile*32+(sy%8)*4+(sx%8)//2]
                    color=(value>>4) if sx%2==0 else (value&15)
                    px,py=origin[0]+p['x']+x,origin[1]+p['y']+y
                    if color and 0<=px<sheet.width and 0<=py<sheet.height:
                        sheet.putpixel((px,py),palette[color])
        draw.text((index%cols*cell+2,index//cols*cell+2),f'{index:02X}',fill='white')
    sheet.save(path)
