The schematic and board were made in Cadsoft EAGLE v4.16r2.

(I need to learn KiCAD someday.)

WARNING: IT MAY BE NECESSARY TO REMOVE THE POWER DIODE ON THE PI PICO!!   

The Pico's Vsys and Vusb rails need to be decoupled, otherwise there may be a chance of magic smoke.

This is because the battery board outputs it's own 5V- to Vsys on the PI, 
and it and it receives its 5V charging voltage from the Pi's Vusb.  
