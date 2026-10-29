#!/usr/bin/env python3
"""QMP keyboard/tablet control for the dedicated rea-win11 reference VM."""
import argparse,json,subprocess,time
VM='rea-win11'
def qmp(command,args):
 p=subprocess.run(['virsh','-c','qemu:///system','qemu-monitor-command',VM,json.dumps({'execute':command,'arguments':args})],check=True,capture_output=True,text=True)
 out=json.loads(p.stdout)
 if 'error' in out: raise RuntimeError(out['error'])
 return out

def event(kind,data):qmp('input-send-event',{'events':[{'type':kind,'data':data}]})
def down(k,value):event('key',{'down':value,'key':{'type':'qcode','data':k}})
def key(name):
 keys=name.split('+')
 for k in keys:down(k,True)
 time.sleep(.06)
 for k in reversed(keys):down(k,False)
 time.sleep(.25)
plain={' ':'spc','-':'minus','=':'equal','[':'bracket_left',']':'bracket_right',';':'semicolon',"'":'apostrophe',',':'comma','.':'dot','/':'slash','\\':'backslash','`':'grave_accent','\n':'ret','\t':'tab'}
shifted={'!':'1','@':'2','#':'3','$':'4','%':'5','^':'6','&':'7','*':'8','(':'9',')':'0','_':'minus','+':'equal','{':'bracket_left','}':'bracket_right',':':'semicolon','"':'apostrophe','<':'comma','>':'dot','?':'slash','|':'backslash','~':'grave_accent'}
def type_text(text):
 for c in text:
  shift=False
  if c.isascii() and c.isalnum():k=c.lower();shift=c.isupper()
  elif c in plain:k=plain[c]
  elif c in shifted:k=shifted[c];shift=True
  else:raise ValueError(repr(c))
  if shift:down('shift',True)
  down(k,True);time.sleep(.012);down(k,False)
  if shift:down('shift',False)
  time.sleep(.015)
def click(x,y,width=1280,height=800):
 qmp('input-send-event',{'events':[{'type':'abs','data':{'axis':'x','value':round(x*32767/(width-1))}},{'type':'abs','data':{'axis':'y','value':round(y*32767/(height-1))}}]})
 event('btn',{'down':True,'button':'left'});time.sleep(.08);event('btn',{'down':False,'button':'left'});time.sleep(.25)
p=argparse.ArgumentParser();s=p.add_subparsers(dest='action',required=True)
s.add_parser('key').add_argument('keys');s.add_parser('type').add_argument('text')
c=s.add_parser('click');c.add_argument('x',type=int);c.add_argument('y',type=int)
s.add_parser('screenshot').add_argument('path')
a=p.parse_args()
if a.action=='key':key(a.keys)
elif a.action=='type':type_text(a.text)
elif a.action=='click':click(a.x,a.y)
else:subprocess.run(['virsh','-c','qemu:///system','screenshot',VM,a.path],check=True)
