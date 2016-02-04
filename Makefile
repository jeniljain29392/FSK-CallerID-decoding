CC              = gcc
CFLAGS          = -DVERBOSE #-Wall -Wextra\
                -pedantic -Wundef -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes \
                -Waggregate-return -Wcast-qual -Wswitch-default -Wunreachable-code -Wformat=2\

INC             = -I ./include
INC_GP          = -I ./include/gnuplot
LDFLAG          = -lpthread
LDFLAG_TA       = -ltinyalsa $(LDFLAG)
TINYALSA        = libtinyalsa.so

FN              = audacity_files/CID_sim_12ko.wav 
LOG             = log.txt

SRC             = ciddeco.c fskmodem.c

PCM_SRC         = pcm.c
PCM_OBJ         = pcm.o
GNUPLOT_SRC     = gnuplot_i.c
GNU_OBJ         = gnuplot_i.o
MAIN            = cid_fsk


all: $(MAIN)
	@echo cid_fsk program is compiled

# /*************************************************************************/
# 	Create a shared library to integrate tinyalsa with Ciddeco.
#	Run Ciddeco to decode CID message directly from sound card.
# /*************************************************************************/

$(MAIN): $(SRC) $(TINYALSA)
	$(CC) $(CFLAGS) $(INC) -Wl,-rpath=$(CURDIR) -o $(MAIN) $(SRC) -L. $(LDFLAG_TA)

$(TINYALSA): $(PCM_OBJ)
	$(CC) -shared -o $(TINYALSA) $(PCM_OBJ)

$(PCM_OBJ): $(PCM_SRC)
	$(CC) -c -fPIC $(PCM_SRC) $(INC)

run: $(MAIN)
	./$(MAIN) 2>$(LOG)

# /*************************************************************************/
# 	Run Ciddeco with gnu plot enabled for debugging waveform
# /*************************************************************************/

gnu_debug: $(SRC) $(GNU_OBJ) $(TINYALSA)
	$(CC) $(CFLAGS) -DDEBUG -DWAVFILE -Wl,-rpath=$(CURDIR) $(INC) -o $(MAIN) $(SRC) $(GNU_OBJ) -L. $(LDFLAG_TA)

$(GNU_OBJ): $(GNUPLOT_SRC)
	$(CC) $(INC_GP) -c -o $(GNU_OBJ) $(GNUPLOT_SRC)

# /*************************************************************************/
# 	Run ciddeco from a sample wav file containing only CID message & no RING
# /*************************************************************************/

wavfile: $(SRC) $(GNU_OBJ) $(TINYALSA)
	$(CC) $(CFLAGS) -DWAVFILE -Wl,-rpath=$(CURDIR) $(INC) -o $(MAIN) $(SRC) -L. $(LDFLAG_TA)

run_file: $(MAIN)
	./$(MAIN) $(FN) 2>$(LOG)

# /*************************************************************************/
# 	Create Doxygen files for documentation
# /*************************************************************************/

doxy: $(SRC)
	doxygen ./Doxyfile

# /*************************************************************************/
# 	Clean all the object files and log files along with main
# /*************************************************************************/

clean:
	rm -f *.o *.png *.txt $(MAIN) $(TINYALSA)
