# Cerbogx-socket-switcher
Reads Battery Voltage on TCP Modbus CerboGX Devices and controls Fritz Dect200 Power Sockets from Configuration.

Webinterface:

![Bildschirmfoto vom 2024-03-09 14-26-08](https://github.com/schuppeste/Cerbogx-socket-switcher/assets/3218517/036e7964-a692-43dc-98a9-82bef9a9885e)


Configuration:  
Active: Enable or Disable Device  
Min: Voltage to switch off Switch (Hystherese)  
Max: Voltage to switch on Switch (Hystherese)  
Invert: OnOff: switch off at max Voltage, Switch on at min Voltage... OffON switch Off at min Voltage, switch on at max Voltage  
delon: On Delay in Minutes  
deloff: Off Delay in Minutes
depon: Depency On  (id (Pos Number in List)0-9) Change state only if Device Id is on
depoff: Depency Off  (id (Pos Number in List)) Change state only if Device Id is off
ontime: switch on Time  
offtime: switch off Time  
itime: change on/off in Between or not Between 
