LFLAGS=-file-line-error -halt-on-error -interaction=batchmode

all: documentation

documentation: documentation.tex title.tex
	pdflatex $(LFLAGS) documentation 
	#biber documentation
	#pdflatex $(LFLAGS) documentation
	#cp documentation.pdf 
