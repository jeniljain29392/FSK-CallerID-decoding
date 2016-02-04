# FSK-CallerID-decoding

Caller ID  is a telephone service, that transmits a caller's number to the called party's telephone equipment
during the ringing signal. Modulation technique used for transmittinf Caller ID is either FSK or DTMF.
FSK CallerID Decoding, demodulatess and decode the caller ID signal coming from the telephone jack and gives
the Caller ID message. 

Folder Structure
- include         : includes the libraries used and the header files
- gnuplot         : includes libraries for ploting the FSK signals
- Doxygen         : Doxygen documentation of the APIs
- audacity_files  : caller ID signals used for decoding the message
- fskmodem.c      : demodulated the FSK caller ID signals
- ciddeco.c       : Decodes the Caller ID message from the demodulated signal
- Makefile        : makefile to compile and run the program.
  
  
