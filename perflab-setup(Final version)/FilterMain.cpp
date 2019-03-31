#include <stdio.h>
#include "cs1300bmp.h"
#include <iostream>
#include <fstream>
#include "Filter.h"
#include <stdlib.h>

using namespace std;

#include "rdtsc.h"

//
// Forward declare the functions
//
Filter * readFilter(string filename);
double applyFilter(Filter *filter, cs1300bmp *input, cs1300bmp *output);

int
main(int argc, char **argv)
{

  if ( argc < 2) {
    fprintf(stderr,"Usage: %s filter inputfile1 inputfile2 .... \n", argv[0]);
  }

  //
  // Convert to C++ strings to simplify manipulation
  //
  string filtername = argv[1];

  //
  // remove any ".filter" in the filtername
  //
  string filterOutputName = filtername;
  string::size_type loc = filterOutputName.find(".filter");
  if (loc != string::npos) {
    //
    // Remove the ".filter" name, which should occur on all the provided filters
    //
    filterOutputName = filtername.substr(0, loc);
  }

  Filter *filter = readFilter(filtername);

  double sum = 0.0;
  int samples = 0;

  for (int inNum = 2; inNum < argc; inNum++) {
    string inputFilename = argv[inNum];
    string outputFilename = "filtered-" + filterOutputName + "-" + inputFilename;
    struct cs1300bmp *input = new struct cs1300bmp;
    struct cs1300bmp *output = new struct cs1300bmp;
    int ok = cs1300bmp_readfile( (char *) inputFilename.c_str(), input);

    if ( ok ) {
      double sample = applyFilter(filter, input, output);
      sum += sample;
      samples++;
      cs1300bmp_writefile((char *) outputFilename.c_str(), output);
    }
    delete input;
    delete output;
  }
  fprintf(stdout, "Average cycles per sample is %f\n", sum / samples);

}

struct Filter *
readFilter(string filename)
{
  ifstream input(filename.c_str());

  if ( ! input.bad() ) {
    int size = 0;
    input >> size;
    Filter *filter = new Filter(size);
    int div;
    input >> div;
    filter -> setDivisor(div);
    for (int i=0; i < size; i++) {
      for (int j=0; j < size; j++) {
	int value;
	input >> value;
	filter -> set(i,j,value);
      }
    }
    return filter;
  } else {
    cerr << "Bad input in readFilter:" << filename << endl;
    exit(-1);
  }
}


double
applyFilter(struct Filter *filter, cs1300bmp *input, cs1300bmp *output)
{

  long long cycStart, cycStop;

  cycStart = rdtscll();

  output -> width = input -> width;
  output -> height = input -> height;

  int Width = input -> width - 1;
  int Height = input -> height - 1;
  int filterdivisor = filter -> getDivisor();
  int output0, output1, output2;
  /*
  made local variables out of function calls and kept them out the loop
  so that the computations would be done less frequently
  */

  int filterMatrix[9] = {
      filter -> get(0,0), filter -> get(0,1), filter -> get(0,2),
      filter -> get(1,0), filter -> get(1,1), filter -> get(1,2),
      filter -> get(2,0), filter -> get(2,1), filter -> get(2,2),
  };

  /*
  I made local array of filter->get, so that less time would be spent going into memory to retrieve values
  */

  int row, col, plane;

/*
    reordered loops so that they would have better spatial locality
    In the nested For loop, if the loop with more iteration is put inside, and the loop with less iteration is put outside,
    its performance will be improved; Reducing the instantiation of loop variables also improves their performance.

    the nested loop read the elements of the array in row-major-order

*/
for( plane = 0; plane < 3; plane++){
  for( row = 1; row < Height ; row++){
      for( col = 1; col < Width; col++){


        int* FILTER_V = &filterMatrix[0];

        /*urolled two loops so that there would be less overhead over iterations*/
        output0 = (input -> color[plane][row-1][col-1] * *(FILTER_V++));
        output1 = (input -> color[plane][row-1][col] * *(FILTER_V++));
        output2 = (input -> color[plane][row-1][col+1] * *(FILTER_V++));

        output0 += (input -> color[plane][row][col-1] * *(FILTER_V++));
        output1 += (input -> color[plane][row][col] * *(FILTER_V++));
        output2 += (input -> color[plane][row][col+1] * *(FILTER_V++));

        output0 += (input -> color[plane][row+1][col-1] * *(FILTER_V++));
		output1 += (input -> color[plane][row+1][col] * *(FILTER_V++));
		output2 += (input -> color[plane][row+1][col+1] * *(FILTER_V++));

		output -> color[plane][row][col] = output0 + output1 + output2;

		/*used three accumulators to hold data and then combined them at the end
		so computations can be done in parallel and there would be less dependency*/


		/*made a condition for divisor so division will not be done or done less frequently if the divisor is 1*/
        if ( filterdivisor > 1){
        output -> color[plane][row][col] /= filterdivisor;
        }

        else if ( output -> color[plane][row][col]  < 0 ){
            output -> color[plane][row][col] = 0;
            }

        else if ( output -> color[plane][row][col]  > 255 ){
            output -> color[plane][row][col] = 255;
            }


      }
    }
  }

  cycStop = rdtscll();
  double diff = cycStop - cycStart;
  double diffPerPixel = diff / (output -> width * output -> height);
  fprintf(stderr, "Took %f cycles to process, or %f cycles per pixel\n",
	  diff, diff / (output -> width * output -> height));
  return diffPerPixel;
}
