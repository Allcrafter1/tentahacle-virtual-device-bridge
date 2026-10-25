-- SPDX-License-Identifier: MIT OR Apache-2.0
-- Run after defining resync_source from aha_control.c's resync_script.
-- No native AHA calls: the entire API is replaced by this local fixture.
local resync=assert((loadstring or load)(resync_source))
local uid='TVB0123456789ABCDEF'
local expected={screen=2,position=8,featuretype=3,name='Existing lamp',
  childrenidentifier={'native-lamp-1'},childrenids={200},featureconfig={unchanged=true}}
local unrelated={screen=1,position=1,featuretype=3,name='Other lamp',
  childrenidentifier={'other'},childrenids={300},featureconfig={other=true}}
local calls,missing,wrongtype,badinterface=0,false,false,false
local widgets={unrelated,expected}
local controller=true
aha={
  GetIDByAIN=function(value)
    if value==uid then return 100 elseif value=='native-lamp-1' then return 200
    elseif value=='other' then return 300 else return 101 end
  end,
  GetDeviceList=function() return {
    {ID=100,Identifyer=uid},{ID=200,Identifyer='native-lamp-1'},
    {ID=16,Identifyer='controller'},{ID=300,Identifyer='other'}} end,
  GetETSIUnit=function(id)
    if id==200 and not missing then return {ETSIDEVICEID=100,UNITID=1,UNITTYPE=wrongtype and 278 or 265} end
  end,
  GetLevelControl=function() if not badinterface then return {level=70} end end,
  GetSimpleOnOff=function() return {State=1} end,
  GetAVMButton=function(id) if id==16 and controller then return {} end end,
  GetAVMButtonScreenConfig=function() return widgets end,
  SetAVMButtonScreenConfig=function(id,screen,position,kind,name,children,config)
    assert(id==16 and screen==2 and position==8 and kind==3 and name==expected.name)
    assert(children==expected.childrenidentifier and config==expected.featureconfig)
    calls=calls+1 -- nil return is the real API contract
  end
}
resync(uid,'dimmable_light');assert(calls==1)
missing=true;assert(not pcall(resync,uid,'dimmable_light'));assert(calls==1);missing=false
wrongtype=true;assert(not pcall(resync,uid,'dimmable_light'));assert(calls==1);wrongtype=false
badinterface=true;assert(not pcall(resync,uid,'dimmable_light'));assert(calls==1);badinterface=false
-- An unrelated missing target cannot prevent a later ready target's resync.
assert(not pcall(resync,'TVB0000000000000002','dimmable_light'));assert(calls==1)
resync(uid,'dimmable_light');assert(calls==2)
-- Startup: another widget on the SAME PAGE still has childrenids={0},
-- although its AIN resolves now. AVM's setter would silently erase its AIN.
unrelated.screen=2;unrelated.childrenids={0}
assert(not pcall(resync,uid,'dimmable_light'));assert(calls==2)
assert(unrelated.childrenidentifier[1]=='other')
-- A reused numeric ID is equally unsafe; never reconstruct from that ID.
unrelated.childrenids={200}
assert(not pcall(resync,uid,'dimmable_light'));assert(calls==2)
unrelated.childrenids={300};unrelated.childrenidentifier={''}
assert(not pcall(resync,uid,'dimmable_light'));assert(calls==2)
unrelated.childrenidentifier={'other'}
resync(uid,'dimmable_light');assert(calls==3)
-- An unresolved widget on ANOTHER PAGE is left completely untouched.
unrelated.screen=1;unrelated.childrenids={0}
resync(uid,'dimmable_light');assert(calls==4)
controller=false;assert(not pcall(resync,uid,'dimmable_light'));assert(calls==4);controller=true
widgets={};assert(not pcall(resync,uid,'dimmable_light'));assert(calls==4)
widgets={unrelated,expected}
expected.childrenidentifier={'unrelated'}
resync(uid,'dimmable_light');assert(calls==4)
print('aha_resync_test: OK')
