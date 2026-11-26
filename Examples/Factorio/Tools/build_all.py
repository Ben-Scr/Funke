"""Owns freeplay scenes, prefabs and UI. Adds missing balance data; migrates the
old contract schema once. Subsequent runs preserve designer tuning."""
from presentation import *

MACHINES = ['drill','belt','furnace','assembler','depot','inserter']
ITEMS = ['ore','stone','copperore','plate','copperplate','gear'] + MACHINES
TITLES = ['Iron ore','Stone','Copper ore','Iron plate','Copper plate','Gear',
          'Mining drill','Conveyor','Furnace','Assembler','Storage chest','Inserter']
ICONS = dict(zip(ITEMS,['ore','rock','copper_deposit','plate','plate','gear','drill','belt','furnace','assembler','crate','']))
BELTS = gid('texture','Machines/ConnectedBelts')
INSERTER = gid('texture','Machines/Inserter')
ARM = gid('texture','Machines/InserterArm')


def mechanical_art():
    # Exact tile edges and topology are generated, so every connection meets at
    # the same coordinate. These are new structural sprites, not edits of AI art.
    size=128
    atlas=Image.new('RGBA',(size*4,size*4))
    slices=[]
    for mask in range(16):
        im=Image.new('RGBA',(size,size)); d=ImageDraw.Draw(im)
        ends=[(128,64),(64,128),(0,64),(64,0)]
        for width,col in [(90,PAL['ink']),(82,PAL['rust']),(70,PAL['steelLight']),(62,PAL['ink']),(54,PAL['steel'])]:
            d.ellipse((64-width//2,64-width//2,64+width//2,64+width//2),fill=col)
            for direction,end in enumerate(ends):
                if mask & (1<<direction): d.line([(64,64),end],fill=col,width=width)
        for direction in range(4):
            if not mask & (1<<direction): continue
            for distance in [20,34,48,62]:
                x,y=64+((1,0,-1,0)[direction])*distance,64+((0,1,0,-1)[direction])*distance
                if direction%2==0: d.line((x,y-24,x,y+24),fill=PAL['ink'],width=4); d.line((x+2,y-23,x+2,y+23),fill=PAL['steelLight'],width=2)
                else: d.line((x-24,y,x+24,y),fill=PAL['ink'],width=4); d.line((x-23,y+2,x+23,y+2),fill=PAL['steelLight'],width=2)
        x,y=mask%4*size,mask//4*size
        atlas.paste(im,(x,y)); slices.append({'name':'belt'+str(mask),'rect':[x,y,size,size],'pivot':[.5,.5]})
    texture('Machines/ConnectedBelts',atlas)
    path=ASSETS/'Textures/Machines/ConnectedBelts.png.meta'
    data=json.loads(path.read_text()); data['sprites']=slices; write_json(path,data)
    base=Image.new('RGBA',(128,128)); d=ImageDraw.Draw(base)
    d.rounded_rectangle((22,28,106,108),radius=10,fill=PAL['ink'])
    d.rounded_rectangle((20,20,104,100),radius=9,fill=PAL['rust'],outline=PAL['steelLight'],width=4)
    for x,y in [(29,29),(95,29),(29,91),(95,91)]: d.ellipse((x-3,y-3,x+3,y+3),fill=PAL['ink'])
    d.ellipse((37,37,91,91),fill=PAL['ink'],outline=PAL['amber'],width=4)
    d.ellipse((46,46,82,82),fill=PAL['steelLight'])
    texture('Machines/Inserter',base)
    arm=Image.new('RGBA',(256,128)); d=ImageDraw.Draw(arm)
    d.line((128,64,238,64),fill=PAL['ink'],width=19)
    d.line((128,61,232,61),fill=PAL['amber'],width=11)
    d.line((133,57,230,57),fill=PAL['text'],width=2)
    d.ellipse((121,55,139,73),fill=PAL['steelLight'],outline=PAL['ink'],width=3)
    d.line([(230,47),(243,47),(243,81),(230,81)],fill=PAL['steelLight'],width=5)
    texture('Machines/InserterArm',arm)


def migrate(path, remove=(), **fields):
    p=ASSETS/'Data'/(path+'.dataasset')
    obj=json.loads(p.read_text())
    for name in remove: obj['fields'].pop(name,None)
    for name,value in fields.items():
        obj['fields'][name]=value[1] if isinstance(value,tuple) and len(value)==2 and isinstance(value[0],str) else enc(value)
    write_json(p,obj)


def content():
    for key,title in zip(ITEMS,TITLES):
        data_asset('Items/'+key,'ItemDefinition',Id=key,DisplayName=title,
                   Icon=INSERTER if key=='inserter' else ATLAS,SpriteName=ICONS[key],StackSize=100)
        path=ASSETS/'Data/Items'/(key+'.dataasset')
        if key=='copperplate' and not isinstance(json.loads(path.read_text())['fields'].get('Tint'),str):
            migrate('Items/'+key,Tint=(1,.57,.3,1))
    recipes=[
        ('plate',1,2,[('ore',1)]),('copperplate',1,2,[('copperore',1)]),('gear',1,1,[('plate',2)]),
        ('drill',1,4,[('plate',8),('gear',3),('stone',5)]),('belt',2,.6,[('plate',1),('gear',1)]),
        ('furnace',1,3,[('stone',8)]),('assembler',1,5,[('plate',10),('gear',5),('copperplate',3)]),
        ('depot',1,1,[('plate',4)]),('inserter',1,1.2,[('plate',2),('gear',1),('copperplate',1)]),
        ('ore',20,0,[]),('stone',20,0,[]),('copperore',20,0,[])]
    for key,amount,seconds,ingredients in recipes:
        data_asset('Recipes/'+key,'RecipeDefinition',Id=key,Result=gid('data','Items/'+key),Amount=amount,Seconds=seconds,
                   CreativeOnly=not ingredients,Ingredients=enc_objects([[gid('data','Items/'+i),n] for i,n in ingredients]))
    descriptions=[
        'Extracts the resource under it. Outputs to a belt or an inserter.',
        'Drag a connected route. R turns the output. Side inputs merge.',
        'Smelts iron or copper ore. Use inserters for automatic loading.',
        'Turns two iron plates into a gear. Inserters load and unload it.',
        'Stores up to 400 items. Load and unload using inserters.',
        'Picks up behind its base and drops in front. R rotates it.']
    for kind,key in enumerate(MACHINES):
        icon=INSERTER if key=='inserter' else ATLAS
        pf=Prefab('Machines/'+key)
        root=pf.add(TITLES[ITEMS.index(key)],T2D(sx=2,sy=2),sprite(BELTS if key=='belt' else icon,'belt5' if key=='belt' else ICONS[key],order=10))
        arrow=pf.add('Output direction',T2D(y=-.32,sx=.34,sy=.34),sprite(ATLAS,'arrow',order=15),parent=root)
        lamp=pf.add('Machine status',T2D(x=.33,y=.33,sx=.13,sy=.13),sprite(ATLAS,'lamp',order=16),parent=root)
        arm=pf.add('Articulated arm',T2D(sx=2,sy=1),sprite(ARM,order=18),parent=root) if key=='inserter' else None
        pf.script(root,'MachineNode',Definition=f_data('MachineDefinition',gid('data','Machines/'+key)),GridX=0,GridY=0,Direction=0)
        fields=dict(Arrow=f_entity(arrow),Indicator=f_comp(lamp,'SpriteRenderer'))
        if arm: fields['Arm']=f_entity(arm)
        pf.script(root,'MachineVisual',**fields); pf.save()
        links=['plate','copperplate'] if key=='furnace' else ['gear'] if key=='assembler' else []
        data_asset('Machines/'+key,'MachineDefinition',Id=key,DisplayName=TITLES[ITEMS.index(key)],Description=descriptions[kind],
                   Kind=kind,CycleSeconds=[1.5,.4,2,2.5,1,.7][kind],Prefab=f_prefab(pf.guid),Icon=icon,SpriteName=ICONS[key],
                   BuildingItem=gid('data','Items/'+key),ConnectionTexture=BELTS if key=='belt' else '0',
                   Recipes=[gid('data','Recipes/'+n) for n in links])
        # Old entries still have Cost. Remove this obsolete economy exactly once.
        path=ASSETS/'Data/Machines'/(key+'.dataasset')
        if 'Cost' in json.loads(path.read_text())['fields']:
            migrate('Machines/'+key,remove=('Cost','Input','Output','InputCount'),Description=descriptions[kind],DisplayName=TITLES[ITEMS.index(key)],
                    BuildingItem=gid('data','Items/'+key),ConnectionTexture=BELTS if key=='belt' else '0',
                    Recipes=[gid('data','Recipes/'+n) for n in links],SpriteName=ICONS[key])
    pf=Prefab('Items/Cargo'); pf.add('Cargo',T2D(sx=.55,sy=.55),sprite(ATLAS,'ore',order=21)); pf.save()
    fields=dict(Machines=[gid('data','Machines/'+n) for n in MACHINES],Items=[gid('data','Items/'+n) for n in ITEMS],
                Recipes=[gid('data','Recipes/'+r[0]) for r in recipes],CargoPrefab=f_prefab(pf.guid),
                CellSize=2,HalfWidth=48,HalfHeight=36,InventorySlots=40,MiningSeconds=.65)
    data_asset('FactoryRules','FactoryRules',**fields)
    if 'GoalCount' in json.loads((ASSETS/'Data/FactoryRules.dataasset').read_text())['fields']:
        migrate('FactoryRules',remove=('GoalItem','GoalCount','StartingCredits','DeadlineSeconds'),**fields)


def world(s,demo=False):
    rng=random.Random(1911)
    for y in range(-5,6):
        for x in range(-7,8):
            s.add(f'Ground {x} {y}',T2D(x*16,y*16,sx=16,sy=16),sprite(gid('texture','Environment/Wasteland'),order=-100))
    patches=[('ore',-8,0),('stone',-3,-4),('copperore',6,4),('ore',-23,17),('copperore',29,-12),('stone',18,22),
             ('ore',36,26),('copperore',-35,-21),('stone',-38,25),('ore',21,-28),('stone',32,-20)]
    for item,ox,oy in patches:
        for dx,dy in [(0,0),(-1,0),(0,1),(1,1),(-1,-1),(1,-1),(2,0),(-2,1)]:
            x,y=ox+dx,oy+dy
            u=s.add(f'{item} deposit {x} {y}',T2D(x*2,y*2,sx=2,sy=2),sprite(ATLAS,{'ore':'iron_deposit','stone':'rock','copperore':'copper_deposit'}[item],order=-5))
            if not demo: s.script(u,'ResourceDeposit',Item=f_data('ItemDefinition',gid('data','Items/'+item)),GridX=x,GridY=y,Amount=500)
    for i in range(230):
        x,y=rng.uniform(-96,96),rng.uniform(-72,72)
        if abs(x)<22 and abs(y)<12: continue
        s.add(f'Scrub {i}',T2D(x,y,sx=1.4,sy=1.4),sprite(ATLAS,rng.choice(['grass','shrub','rock']),order=-6))
    if demo:
        for key,x in [('drill',-8),('furnace',-3),('assembler',3),('depot',8)]:
            s.add(key,T2D(x*2,0,sx=2,sy=2),sprite(ATLAS,ICONS[key],order=10))
        for x in [-7,-6,-5,-4,-2,-1,0,1,2,4,5,6,7]:
            s.add(f'Belt {x}',T2D(x*2,0,sx=2,sy=2),sprite(BELTS,'belt5',order=5))


def inventory(s,ui):
    root,p=modal(s,'Inventory and crafting',(1200,732))
    title=label(s,'Inventory title','INVENTORY',(-290,310),(540,38),p,font_size=27,align=0,order=202)
    label(s,'Craft title','CRAFTING',(290,310),(470,38),p,font_size=27,align=0,order=202)
    btn(s,'Close inventory','X',(554,313),(48,44),ui,'FactoryInventoryPanel','Close',p,order=210)
    for i in range(40):
        slot=panel(s,f'Stack {i}',(-527+i%8*70,232-i//8*70),(64,64),p,order=203,style='Button',block=False)
        s.by_uuid[slot].update(button(click(slot,'InventorySlot','Select')))
        hi=panel(s,f'Stack selected {i}',(0,0),(64,64),slot,order=204,style='Selected',disabled=True,block=False)
        icon=s.add('Stack icon '+str(i),rect((0,5),(46,46)),image(ATLAS,slice='ore',order=205),parent=slot)
        count=label(s,'Stack count '+str(i),'',(12,-18),(40,24),slot,font_size=17,order=206)
        s.script(slot,'InventorySlot',Index=i,Icon=f_comp(icon,'Image'),Count=f_comp(count,'TextRenderer'),Highlight=f_entity(hi))
    for i in range(12):
        slot=panel(s,f'Recipe {i}',(101+i%4*120,220-i//4*112),(112,104),p,order=203,style='Button',block=False)
        s.by_uuid[slot].update(button(click(slot,'RecipeSlot','Craft')))
        icon=s.add('Recipe icon '+str(i),rect((0,19),(46,46)),image(ATLAS,slice='ore',order=205),parent=slot)
        caption=label(s,'Recipe label '+str(i),'',(0,-28),(108,42),slot,font_size=15,order=206)
        s.script(slot,'RecipeSlot',Index=i,Icon=f_comp(icon,'Image'),Caption=f_comp(caption,'TextRenderer'))
    selected=label(s,'Selected stack','Select a stack',(-290,-99),(540,32),p,font_size=19,color='amber',align=0,order=202)
    detail=label(s,'Recipe details','',(280,-106),(480,104),p,font_size=18,align=0,order=202)
    queue=label(s,'Craft queue','',(-90,-184),(960,36),p,font_size=20,align=0,order=202)
    btn(s,'Cancel crafting','Cancel last',(477,-184),(170,44),ui,'FactoryInventoryPanel','Cancel',p,order=210)
    machine=label(s,'Machine contents','',(-120,-257),(880,68),p,font_size=20,align=0,order=202)
    btn(s,'Insert selected','Insert selected',(460,-250),(210,46),ui,'FactoryInventoryPanel','Feed',p,order=210)
    btn(s,'Take contents','Take contents',(460,-309),(210,46),ui,'FactoryInventoryPanel','Take',p,order=210)
    s.script(ui,'FactoryInventoryPanel',Panel=f_entity(root),Title=f_comp(title,'TextRenderer'),Detail=f_comp(detail,'TextRenderer'),
             QueueLabel=f_comp(queue,'TextRenderer'),MachineLabel=f_comp(machine,'TextRenderer'),SelectedLabel=f_comp(selected,'TextRenderer'))


def factory_scene():
    s=Scene('Factory'); world(s)
    cam=s.add('Construction camera',T2D(),Camera2D={'orthoSize':14,'clearR':.1,'clearG':.12,'clearB':.08,'zoom':1})
    s.script(cam,'FactoryCamera',Camera=f_comp(cam,'Camera2D'))
    manager=s.add('Factory session')
    s.script(manager,'FactorySession',Rules=f_data('FactoryRules',gid('data','FactoryRules')),MenuScene=f_scene(gid('scene','MainMenu')),FactoryScene=f_scene(s.guid))
    s.script(manager,'FactoryCrafting'); s.script(manager,'FactorySimulation')
    s.script(manager,'FactorySave',Camera=f_comp(cam,'Camera2D'))
    s.script(manager,'FactoryFeedback',PlaceSound=f_audio(gid('audio','Place')))
    preview=s.add('Placement ghost',T2D(sx=2,sy=2),sprite(ATLAS,'belt',order=40),disabled=True)
    arrow=s.add('Preview direction',T2D(y=-.35,sx=.3,sy=.3),sprite(ATLAS,'arrow',order=41),parent=preview)
    s.script(manager,'FactoryBuilder',Camera=f_comp(cam,'Camera2D'),Preview=f_entity(preview),PreviewArrow=f_entity(arrow))
    ui=s.add('Factory HUD',RECT_FILL)
    top=panel(s,'Factory toolbar',(0,-16),(1120,64),ui,anchor=(.5,1))
    mode=label(s,'Game mode','FREEPLAY',(-430,0),(190,36),top,font_size=22,color='amber')
    queue=label(s,'Queue summary','',(-120,0),(380,36),top,font_size=20)
    btn(s,'Inventory','Inventory',(210,0),(180,44),ui,'FactoryInventoryPanel','Toggle',top)
    btn(s,'Help','?',(350,0),(56,44),ui,'FactoryPanels','OpenHelp',top)
    btn(s,'Pause','Pause',(455,0),(130,44),ui,'FactoryPanels','TogglePause',top)
    tray=panel(s,'Construction tray',(0,16),(1144,122),ui,anchor=(.5,0))
    for i,key in enumerate(MACHINES):
        slot=panel(s,'Build '+key,(-470+i*188,0),(178,104),tray,style='Button',block=False)
        s.by_uuid[slot].update(button(click(slot,'BuildSlot','Select')))
        hi=panel(s,key+' selected',(0,0),(178,104),slot,order=101,style='Selected',disabled=True,block=False)
        s.add(key+' icon',rect((0,19),(58,58)),image(INSERTER if key=='inserter' else ATLAS,slice=ICONS[key],order=102),parent=slot)
        count=label(s,key+' count','',(0,-29),(166,42),slot,font_size=17)
        s.script(slot,'BuildSlot',Index=i,Highlight=f_entity(hi),Cost=f_comp(count,'TextRenderer'))
    notice=label(s,'Notice','',(0,176),(1300,30),ui,font_size=20,anchor=(.5,0))
    context=label(s,'Context','',(0,146),(1300,26),ui,font_size=18,color='muted',anchor=(.5,0))
    s.script(ui,'FactoryHud',Mode=f_comp(mode,'TextRenderer'),Queue=f_comp(queue,'TextRenderer'),Notice=f_comp(notice,'TextRenderer'),Context=f_comp(context,'TextRenderer'))
    inventory(s,ui)
    pause,pp=modal(s,'Pause screen',(540,540))
    label(s,'Paused title','FACTORY PAUSED',(0,210),(450,50),pp,font_size=32,order=202)
    for title,method,y in [('Resume','Resume',120),('Save factory','Save',48),('Load factory','Load',-24),('New factory','Restart',-96),('Main menu','Menu',-168)]:
        btn(s,'Pause '+method,title,(0,y),(380,56),ui,'FactoryPanels',method,pp,order=210)
    help_root,hp=modal(s,'Controls',(850,650))
    label(s,'Help title','YOUR FACTORY. YOUR PACE.',(0,267),(780,50),hp,font_size=30,order=202)
    help_text='WASD / middle drag   Explore     /     Wheel   Zoom\nF (hold over a deposit)   Gather iron, copper or stone\nE   Inventory + crafting     /     Shift-click recipe   Craft 5\n1-6   Select building     /     Q   Empty hand\nLeft drag   Build     /     R   Rotate     /     Right drag   Remove\nClick a machine with an empty hand to access its items.\nSelect a stack, then Insert selected (Shift: 10).\n\nDrill > Belt > Inserter > Furnace > Inserter > Belt\nInserters pick up behind and drop in front.\nF5   Save     /     F9   Load     /     Esc   Close or pause'
    label(s,'Control reference',help_text,(0,16),(760,424),hp,font_size=22,align=0,order=202)
    btn(s,'Close help','Back to factory',(0,-267),(370,56),ui,'FactoryPanels','CloseHelp',hp,order=210)
    s.script(ui,'FactoryPanels',PausePanel=f_entity(pause),HelpPanel=f_entity(help_root))
    s.save()


def menu_scene():
    s=Scene('MainMenu'); world(s,demo=True)
    s.add('Menu camera',T2D(),Camera2D={'orthoSize':13,'clearR':.1,'clearG':.12,'clearB':.08,'zoom':1})
    ui=s.add('Main menu',RECT_FILL)
    s.add('Menu shade',RECT_FILL,image(color='#121719A8',order=90),parent=ui)
    p=panel(s,'Menu frame',(0,0),(580,740),ui)
    label(s,'Brand','RUST & STEEL',(0,282),(500,75),p,font_size=56,color='amber')
    label(s,'Subtitle','AN INDUSTRIAL FRONTIER',(0,224),(490,40),p,font_size=20,color='muted')
    for j,key in enumerate(['drill','furnace','assembler']):
        s.add('Menu '+key,rect((-104+j*104,155),(84,84)),image(ATLAS,slice=key,order=103),parent=p)
    btn(s,'New factory','New factory',(0,60),(420,62),ui,'FactoryMenu','Play',p)
    btn(s,'Creative mode','Creative mode',(0,-18),(420,62),ui,'FactoryMenu','Creative',p)
    cont=btn(s,'Load saved','Continue factory',(0,-96),(420,62),ui,'FactoryMenu','Continue',p)
    btn(s,'Settings','Settings',(0,-174),(420,62),ui,'FactoryMenu','Settings',p)
    btn(s,'Quit','Quit',(0,-252),(420,62),ui,'FactoryMenu','Quit',p)
    note=label(s,'Menu notice','',(0,-320),(500,35),p,font_size=18,color='muted')
    settings,sp=modal(s,'Settings screen',(540,430))
    label(s,'Settings title','SETTINGS',(0,140),(450,60),sp,font_size=34,order=202)
    btn(s,'Sound','Sound: ON',(0,54),(380,60),ui,'FactoryMenu','ToggleSound',sp,order=210)
    soundlabel=s.entities[-1]['uuid']
    btn(s,'Fullscreen','Toggle fullscreen',(0,-24),(380,60),ui,'FactoryMenu','ToggleFullscreen',sp,order=210)
    btn(s,'Close settings','Back',(0,-126),(380,60),ui,'FactoryMenu','CloseSettings',sp,order=210)
    s.script(ui,'FactoryMenu',FactoryScene=f_scene(gid('scene','Factory')),SettingsPanel=f_entity(settings),
             SoundLabel=f_comp(soundlabel,'TextRenderer'),Notice=f_comp(note,'TextRenderer'),ContinueButton=f_entity(cont))
    s.save()


def main():
    art(); mechanical_art(); content(); factory_scene(); menu_scene()
    for path in (ASSETS/'Scripts').rglob('*.cs'): meta(path,gid('script',path.stem))
    print('Generated freeplay: connected belts, inserters, storage, 40 inventory slots and 12 recipes.')

if __name__=='__main__': main()
