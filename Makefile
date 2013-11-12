all: t-vpack/t-vpack vpr/bin/vpr

t-vpack/t-vpack:
	cd t-vpack && make;

vpr/bin/vpr:
	cd vpr && make;

clean:
	cd t-vpack && make clean;
	cd vpr && make clean;
