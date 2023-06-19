The schematic and board were made in Cadsoft EAGLE v4.16r2.

(I need to learn KiCAD someday.)

WARNING: TO PREVENT MAGIC SMOKE, REMOVE THE POWER DIODE FROM THE PI PICO !!!

This is because the battery board outputs it's own 5V, which runs to 
Vsys on the PI, and it and it receives its 5V charging voltage from the Pi's
Vusb.  (These two rails need to be decoupled on the Pi board.)
