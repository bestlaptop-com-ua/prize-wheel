import math
c=.55;b=.28
best=(-1e9,None)
for i in range(20,721):
 v=i/1000;w=v*2*math.pi;natural=((w/b)-(c/(b*b))*math.log1p(b*w/c))*180/math.pi
 minimum=v*.22*360+180*min(.95*v,.68)**2/.1+2
 gap=natural-2-minimum
 if gap>best[0]:best=(gap,v)
 if i in (50,100,150,200,300,472,600,720):print('speed',v,'naturalDeg',round(natural,2),'minGentleRunway',round(minimum,2),'shadowSlack',round(gap,2))
print('Best shadow slack across20..720mrev/s',best)