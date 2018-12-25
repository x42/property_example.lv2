LV2 Parameter/Property Example
==============================

This simple [LV2](http://lv2plug.in) plugin is provided as example
to show how to use LV2 parameter properties as control values.

This plugin is a simple dezippered amplifier that exposes two parameters
as properties: a float (gain in dB) and a boolean (polarity invert).

The plugin itself has no control data inputs. Both properties are set
using a single LV2 Atom control port.

See also

* http://lv2plug.in/ns/lv2core/lv2core.html#Parameter
* http://lv2plug.in/ns/ext/patch/#Set
* http://lv2plug.in/ns/ext/patch/#writable

Build and Install
-----------------

```bash
make
#sudo make install PREFIX=/usr
ln -s "$(pwd)/build" ~/.lv2/property_example.lv2
```
