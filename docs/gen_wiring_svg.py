out=[]; w=out.append
W,H=1560,1080
w(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="Helvetica, Arial, sans-serif">')
w(f'<rect width="{W}" height="{H}" fill="#fafafa"/>')
w('<text x="30" y="38" font-size="24" font-weight="bold" fill="#111">Prize wheel v2 — wiring scheme</text>')
w('<text x="30" y="58" font-size="14" fill="#555">ESP32-S3 (Lonely Binary, screw-terminal breakout) · TMC5160T Pro · AS5600 I2C · WS2815 halo · PCM5102A -&gt; TPA3116D2</text>')
COL={'24':'#d62828','12':'#f77f00','5':'#e91e63','33':'#c9a800','gnd':'#111','sig':'#1d4ed8','spi':'#7c3aed','i2c':'#0891b2','aud':'#0f766e'}
def box(x,y,bw,bh,title,pins_l=(),pins_r=(),note='',fill='#fff'):
    w(f'<rect x="{x}" y="{y}" width="{bw}" height="{bh}" rx="8" fill="{fill}" stroke="#333" stroke-width="2"/>')
    w(f'<text x="{x+bw/2}" y="{y+22}" font-size="15" font-weight="bold" text-anchor="middle" fill="#111">{title}</text>')
    if note: w(f'<text x="{x+bw/2}" y="{y+39}" font-size="10.5" text-anchor="middle" fill="#555">{note}</text>')
    P={}
    for i,p in enumerate(pins_l):
        py=y+58+i*20; P[p]=(x,py)
        w(f'<circle cx="{x}" cy="{py}" r="4" fill="#333"/><text x="{x+8}" y="{py+4}" font-size="11.5" fill="#111">{p}</text>')
    for i,p in enumerate(pins_r):
        py=y+58+i*20; P[p]=(x+bw,py)
        w(f'<circle cx="{x+bw}" cy="{py}" r="4" fill="#333"/><text x="{x+bw-8}" y="{py+4}" font-size="11.5" text-anchor="end" fill="#111">{p}</text>')
    return P
def wire(a,b,c,label='',via=None,dash=False,lw=2.2,lo=(4,-5)):
    pts=[a]+(via or [])+[b]
    d=' '.join(f'{x},{y}' for x,y in pts)
    da='stroke-dasharray="6,4" ' if dash else ''
    w(f'<polyline points="{d}" fill="none" stroke="{COL[c]}" stroke-width="{lw}" {da}stroke-linejoin="round"/>')
    if label:
        mx,my=(pts[1] if len(pts)>2 else ((a[0]+b[0])/2,(a[1]+b[1])/2))
        w(f'<text x="{mx+lo[0]}" y="{my+lo[1]}" font-size="11" fill="{COL[c]}">{label}</text>')

# power row
P24=box(30,80,190,105,'24 V PSU','',('V+','V-'),'200 W LED driver, 8.33 A')
P12=box(255,80,190,105,'12 V PSU','',('V+','V-'),'Mean Well LRS-100-12')
BK =box(480,80,190,105,'Buck 12-&gt;5 V',('IN+','IN-'),('OUT+','OUT-'),'3 A, logic + DAC')
GND=box(705,80,165,105,'STAR GND',(),(),'one bolt / terminal block',fill='#eee')
USB=box(900,80,210,105,'USB isolator',(),('USB',),'ADuM4160 -&gt; laptop')
FAN=box(1145,80,150,105,'Fan 24 V',('+','-'),(),'on driver heatsink')

# core
S3 =box(545,250,300,450,'ESP32-S3  (screw-terminal breakout)',
        '5V IN,3V3,GND,GPIO40,GPIO38 SDA,GPIO39 SCL,GPIO15,GPIO16,GPIO17'.split(','),
        'GPIO12 SCK,GPIO11 MOSI,GPIO13 MISO,GPIO10 CS,GPIO5 STEP,GPIO6 DIR,GPIO7 EN'.split(','),
        'leave 19/20 (USB) and 43/44 (console) empty')
TMC=box(960,250,250,290,'TMC5160T Pro stepstick',
        'VIO,GND,CFG2 SCK,CFG1 MOSI,CFG0 MISO,CFG3 CS,STP,DIR,EN,CLK'.split(','),
        'VM,GND pwr,A1,A2,B1,B2'.split(','),
        'CFG0-3 + CLK pins bent out, jumpered')
MOT=box(1290,270,150,170,'NEMA23 76 mm',('A+','A-','B+','B-'),(),'dual shaft, 3 A')
ENC=box(960,590,250,130,'AS5600 (I2C 0x36)',('3V3','GND','SDA','SCL'),(),'24 AWG twisted, &lt;30 cm · 2.2k pull-ups')

# leds
LS =box(240,250,220,145,'74AHCT125',('VCC 5V','GND','A1 in','/OE -&gt; GND'),('Y1 out',),'DIP-14 screw adapter · 100 nF')
LED=box(30,445,430,140,'WS2815 halo ring  (40", ~300 px)',('+12 V','GND','DI data','BI backup'),(),'inject 12 V both ends · 330 ohm on DI')

# audio
DAC=box(500,780,240,150,'PCM5102A DAC',('VIN 5V','GND','BCK','LRCK','DIN'),('LOUT',),'SCK-&gt;GND · XSMT-&gt;3V3 · FLT/DEMP/FMT open')
AMP=box(880,780,230,150,'TPA3116D2 amp',('VCC 24V','GND','IN L','IN GND'),('OUT+','OUT-'),'fused 3 A')
SPK=box(1200,790,160,110,'Speaker',('+','-'),(),'Dayton B652, 8 ohm')

# ---- power wiring ----
wire(P24['V+'],TMC['VM'],'24',label='24 V  fuse 5 A  18 AWG',via=[(235,148),(235,62),(1520,62),(1520,255),(1245,255),(1245,TMC['VM'][1])])
wire((1520,255),AMP['VCC 24V'],'24',label='fuse 3 A',via=[(1520,755),(865,755),(865,AMP['VCC 24V'][1])],dash=True,lo=(-70,-6))
wire((1300,62),FAN['+'],'24',via=[(1300,66),(1130,66),(1130,FAN['+'][1])])
wire(P24['V-'],(760,138),'gnd',label='heavy GND',via=[(232,168),(232,205),(760,205)],lw=3)
wire(P12['V+'],BK['IN+'],'12',label='12 V',via=[(460,128),(460,BK['IN+'][1])])
wire(P12['V+'],LED['+12 V'],'12',label='12 V  fuse 5 A  14 AWG',via=[(465,128),(465,225),(14,225),(14,LED['+12 V'][1])])
wire(P12['V-'],(770,150),'gnd',via=[(462,168),(462,212),(770,212)],lw=3)
wire(LED['GND'],(770,160),'gnd',label='',via=[(10,LED['GND'][1]),(10,240),(775,240),(775,160)],lw=3,lo=(4,14))
wire(BK['OUT+'],S3['5V IN'],'5',label='5 V',via=[(690,128),(690,232),(528,232),(528,S3['5V IN'][1])])
wire((528,S3['5V IN'][1]),LS['VCC 5V'],'5',via=[(528,236),(228,236),(228,LS['VCC 5V'][1])],dash=True)
wire((528,S3['5V IN'][1]),DAC['VIN 5V'],'5',via=[(528,745),(485,745),(485,DAC['VIN 5V'][1])],dash=True)
wire(BK['OUT-'],(780,170),'gnd',via=[(686,148),(686,178),(780,178)],lw=3)
wire((800,135),S3['GND'],'gnd',label='logic GND',via=[(880,135),(880,243),(522,243),(522,S3['GND'][1])],lw=2.4)
wire(TMC['GND pwr'],(800,182),'gnd',label='driver pwr GND',via=[(1250,TMC['GND pwr'][1]),(1250,247),(880,247),(880,190),(800,190)],lw=3,lo=(-95,-6))
wire(S3['3V3'],TMC['VIO'],'33',label='3V3',via=[(524,S3['3V3'][1]),(524,239),(935,239),(935,TMC['VIO'][1])])
wire((935,TMC['VIO'][1]),ENC['3V3'],'33',via=[(935,ENC['3V3'][1])],dash=True)
wire(S3['GND'],TMC['GND'],'gnd',label='logic ref',via=[(518,S3['GND'][1]),(518,252),(944,252),(944,TMC['GND'][1])],dash=True,lo=(4,12))
wire((944,TMC['GND'][1]),ENC['GND'],'gnd',via=[(944,ENC['GND'][1])],dash=True)
wire(TMC['CLK'],(944,TMC['CLK'][1]),'gnd',label='CLK-&gt;GND',lo=(-70,-5))

# ---- SPI ----
for i,(sp,tp) in enumerate([('GPIO12 SCK','CFG2 SCK'),('GPIO11 MOSI','CFG1 MOSI'),('GPIO13 MISO','CFG0 MISO'),('GPIO10 CS','CFG3 CS')]):
    x=855+i*16
    wire(S3[sp],TMC[tp],'spi',label=sp.split()[1],via=[(x,S3[sp][1]),(x,TMC[tp][1])])
# STEP/DIR/EN
for i,(sp,tp) in enumerate([('GPIO5 STEP','STP'),('GPIO6 DIR','DIR'),('GPIO7 EN','EN')]):
    x=895+i*14
    wire(S3[sp],TMC[tp],'sig',label=sp.split()[1],via=[(x,S3[sp][1]),(x,TMC[tp][1])])
w('<text x="20" y="612" font-size="11" fill="#111">LED GND -&gt; 12 V PSU V- (heavy 14 AWG)</text>')
w('<text x="962" y="556" font-size="11" fill="#555">EN is ACTIVE LOW: drive LOW to run</text>')
# motor phases
for i,(tp,mp) in enumerate([('A1','A+'),('A2','A-'),('B1','B+'),('B2','B-')]):
    x=1245+i*9
    wire(TMC[tp],MOT[mp],'sig',via=[(x,TMC[tp][1]),(x,MOT[mp][1])],lw=2.6)
w('<text x="1290" y="462" font-size="11" fill="#555">front shaft -&gt; L075 -&gt; 3/4" wheel shaft</text>')
w('<text x="1290" y="478" font-size="11" fill="#555">rear stub -&gt; diametric magnet -&gt; AS5600</text>')
w('<text x="1290" y="494" font-size="11" fill="#555">2200 uF / 35 V across VM-GND at driver</text>')

# ---- I2C ----
wire(S3['GPIO38 SDA'],ENC['SDA'],'i2c',label='SDA',via=[(512,S3['GPIO38 SDA'][1]),(512,722),(925,722),(925,ENC['SDA'][1])],lo=(-46,-5))
wire(S3['GPIO39 SCL'],ENC['SCL'],'i2c',label='SCL',via=[(506,S3['GPIO39 SCL'][1]),(506,730),(931,730),(931,ENC['SCL'][1])],lo=(-46,-5))

# ---- LED data ----
wire(S3['GPIO40'],LS['A1 in'],'sig',label='GPIO40 data',via=[(505,S3['GPIO40'][1]),(505,415),(225,415),(225,LS['A1 in'][1])])
wire(LS['Y1 out'],LED['DI data'],'sig',label='5 V data 330 ohm',via=[(478,LS['Y1 out'][1]),(478,430),(18,430),(18,LED['DI data'][1])])
wire(LED['BI backup'],(14,LED['BI backup'][1]),'gnd',label='BI-&gt;GND',lo=(-62,16))
wire(LS['/OE -&gt; GND'],(232,LS['/OE -&gt; GND'][1]),'gnd')
wire(LS['GND'],(232,LS['GND'][1]),'gnd')

# ---- audio ----
wire(S3['GPIO15'],DAC['BCK'],'aud',label='BCK',via=[(500,S3['GPIO15'][1]),(500,760),(470,760),(470,DAC['BCK'][1])])
wire(S3['GPIO16'],DAC['LRCK'],'aud',label='LRCK',via=[(494,S3['GPIO16'][1]),(494,766),(464,766),(464,DAC['LRCK'][1])])
wire(S3['GPIO17'],DAC['DIN'],'aud',label='DIN',via=[(488,S3['GPIO17'][1]),(488,772),(458,772),(458,DAC['DIN'][1])])
wire(DAC['LOUT'],AMP['IN L'],'aud',label='line out (shielded)',via=[(810,DAC['LOUT'][1]),(810,AMP['IN L'][1])])
wire(AMP['IN GND'],(865,AMP['IN GND'][1]),'gnd')
wire(AMP['GND'],(865,AMP['GND'][1]),'gnd',label='-&gt; star',lo=(-46,14))
wire(DAC['GND'],(485,DAC['GND'][1]),'gnd',label='-&gt; star',lo=(-48,-5))
wire(AMP['OUT+'],SPK['+'],'sig',via=[(1160,AMP['OUT+'][1]),(1160,SPK['+'][1])],lw=2.6)
wire(AMP['OUT-'],SPK['-'],'sig',via=[(1170,AMP['OUT-'][1]),(1170,SPK['-'][1])],lw=2.6)
w('<text x="1198" y="920" font-size="11" fill="#555">bridged output: neither terminal to GND</text>')
wire(FAN['-'],(1140,FAN['-'][1]),'gnd')
w('<text x="905" y="200" font-size="11" fill="#555">USB -&gt; S3 native USB port (program/tune only)</text>')

# legend
lx,ly=30,985
for i,(k,name) in enumerate([('24','24 V'),('12','12 V'),('5','5 V'),('33','3V3'),('gnd','GND'),('sig','logic / motor'),('spi','SPI'),('i2c','I2C'),('aud','audio')]):
    x=lx+i*160; w(f'<line x1="{x}" y1="{ly}" x2="{x+36}" y2="{ly}" stroke="{COL[k]}" stroke-width="3"/><text x="{x+44}" y="{ly+4}" font-size="12.5" fill="#111">{name}</text>')
w('<text x="30" y="1020" font-size="11.5" fill="#555">Dashed = shared rail continuation. Fuses: 24 V motor 5 A, 24 V amp 3 A, 12 V LEDs 5 A. All grounds meet once at STAR GND.</text>')
w('<text x="30" y="1038" font-size="11.5" fill="#555">Driver SPI mode 3. AS5600 on its own I2C bus (GPIO38/39, 400 kHz). PCM5102A: SCK to GND and XSMT to 3V3 are required jumpers.</text>')
w('<text x="30" y="1056" font-size="11.5" fill="#555">If STEP/DIR/EN terminals 5/6/7 are absent on the breakout, substitute 47/48/21 and keep the rest unchanged.</text>')
w('</svg>')
open('prize_wheel_v2_wiring.svg','w').write('\n'.join(out))
print('ok')
