# GsmMon
GSM home monitor

![GSM Home monitor](https://github.com/olegv142/GsmMon/blob/master/images/GsmMon.jpg)

This device is designed to monitor and report via SMS the temperature and humidity inside the home
and be able to turn on/off some AC powered equipment (e.g. the heater) on receiving SMS from remote operator.
The are 2 temperature channels with the optional second one designed to monitor the water temperature in
the home heating system.

The SMS message sent by operator always starts from the # symbol.
It may be followed by the following tokens separated by the space or comma:
```
  1        turn on  AC switch
  0        turn off AC switch
  /n       set reporting interval to n hours
  p<PIN>   authenticate with <PIN>
  P<PIN>   set new <PIN>
```
The response will be sent to any authenticated message, even the empty one
(consisting from the single # symbol).
The sender is authenticated implicitly unless the PIN is set and the sender address differs
from the address used previously. In such case the new sender should provide PIN in order
to be authenticated.

The SMS sent back as the response will contain the current AC switch state (as 0 or 1),
temperature and humidity readings, reporting interval and GSM network signal quality metric.

The controller being used is Arduino pro mini 3.3v 8MHz (ATMega328).

## Author

Oleg Volkov (olegv142@gmail.com)


