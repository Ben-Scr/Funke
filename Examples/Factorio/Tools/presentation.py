"""Regenerate authored scenes/prefabs/UI; existing balance data is preserved.
The original ImageGen atlas is used unmodified, with metadata-only slices.
"""
import io
import math
import random
import struct
import wave
from PIL import Image, ImageDraw
from funke_kit import *

PAL = json.loads((PROJECT / 'Design/palette.json').read_text())
ATLAS = gid('texture', 'Machines/FactoryAtlas')
FONT = meta(ASSETS / 'Fonts/Rubik.ttf', gid('font', 'Rubik'))
SLICES = ['drill', 'belt', 'furnace', 'assembler', 'depot', 'iron_deposit', 'copper_deposit', 'grass',
          'ore', 'plate', 'gear', 'crate', 'shrub', 'rock', 'arrow', 'lamp']


def texture(name, canvas, border=None):
    path = ASSETS / 'Textures' / (name + '.png')
    guid = meta(path, gid('texture', name))
    info = {'AssetGUID': guid, 'uuid': guid, 'kind': 'Texture', 'import': {'filter': 'Bilinear', 'wrapU': 'Clamp', 'wrapV': 'Clamp'}}
    if border: info['border'] = list(border)
    write_json(path.with_suffix('.png.meta'), info)
    buf = io.BytesIO(); canvas.save(buf, format='PNG'); write_bytes(path, buf.getvalue())
    return guid


def art():
    path = ASSETS / 'Textures/Machines/FactoryAtlas.png'
    w, h = Image.open(path).size
    slices = []
    for n, name in enumerate(SLICES):
        x, y = round((n % 4) * w / 4), round((n // 4) * h / 4)
        right, bottom = round((n % 4 + 1) * w / 4), round((n // 4 + 1) * h / 4)
        slices.append({'name': name, 'rect': [x, y, right-x, bottom-y], 'pivot': [.5, .5]})
    write_json(path.with_suffix('.png.meta'), {'AssetGUID': ATLAS, 'uuid': ATLAS, 'kind': 'Texture',
        'import': {'filter': 'Bilinear', 'wrapU': 'Clamp', 'wrapV': 'Clamp'}, 'sprites': slices})
    ground = ASSETS/'Textures/Environment/Wasteland.png'
    ground_guid = meta(ground, gid('texture','Environment/Wasteland'))
    write_json(ground.with_suffix('.png.meta'), {'AssetGUID':ground_guid,'uuid':ground_guid,'kind':'Texture',
        'import':{'filter':'Bilinear','wrapU':'Clamp','wrapV':'Clamp'}})
    for name, fill, rim in [('Panel', 'steel', 'steelLight'), ('Button', 'steel', 'rust'), ('Selected', 'rust', 'amber')]:
        im = Image.new('RGBA', (64, 64)); d = ImageDraw.Draw(im)
        d.rounded_rectangle((1, 1, 62, 62), radius=5, fill=PAL['ink'])
        d.rounded_rectangle((2, 2, 60, 59), radius=4, fill=PAL[rim])
        d.rectangle((5, 5, 57, 56), fill=PAL[fill])
        d.line((6, 6, 56, 6), fill=PAL['steelLight'])
        for x, y in [(8, 8), (54, 8), (8, 52), (54, 52)]:
            d.ellipse((x-2, y-2, x+2, y+2), fill=PAL['ink']); d.point((x-1,y-1),fill=PAL['steelLight'])
        texture('UI/' + name, im, (12, 12, 12, 12))
    for name, freq in [('Place', 220), ('Ship', 660), ('Complete', 880)]:
        path = ASSETS / 'Audio/SFX' / (name + '.wav'); meta(path, gid('audio', name))
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as out:
            out.setnchannels(1); out.setsampwidth(2); out.setframerate(22050)
            duration = .08 if name == 'Place' else .18
            values = [int(4000 * math.exp(-i / 750) * math.sin(2*math.pi*freq*i/22050)) for i in range(int(22050*duration))]
            out.writeframes(struct.pack('<' + 'h'*len(values), *values))
        write_bytes(path, buf.getvalue())


def panel(s, name, pos, size, parent=None, anchor=(.5,.5), order=100, style='Panel', disabled=False, block=True):
    return s.add(name, rect(pos, size, anchor), image(gid('texture','UI/'+style),order=order,border=(12,12,12,12)),
        parent=parent, disabled=disabled, **({'Interactable':{'interactable':True,'focusable':False}} if block else {}))


def label(s, name, value, pos, size, parent=None, font_size=24, color='text', order=102, align=1, anchor=(.5,.5)):
    return s.add(name, rect(pos,size,anchor), text(value,font_size,PAL[color],align=align,order=order,font=FONT),parent=parent)


def btn(s,name,title,pos,size,target,cls,method,parent=None,arg=None,order=110):
    u=panel(s,name,pos,size,parent,order=order,style='Button',block=False)
    s.by_uuid[u].update(button(click(target,cls,method,arg),normal='#FFFFFF',hovered=PAL['amber'],pressed=PAL['rust']))
    label(s,name+' label',title,(0,0),(size[0]-24,size[1]-12),u,font_size=23,order=order+1)
    return u


def modal(s, name, size):
    root=s.add(name,rect(size=(4000,2400),fill=True),image(color='#121719DB',order=200),
        disabled=True,Interactable={'interactable':True,'focusable':False})
    p=panel(s,name+' frame',(0,0),size,root,order=201)
    return root,p


