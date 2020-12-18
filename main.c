
/*********************************************************************************************

    This is public domain software that was developed by or for the U.S. Naval Oceanographic
    Office and/or the U.S. Army Corps of Engineers.

    This is a work of the U.S. Government. In accordance with 17 USC 105, copyright protection
    is not available for any work of the U.S. Government.

    Neither the United States Government, nor any employees of the United States Government,
    nor the author, makes any warranty, express or implied, without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, or assumes any liability or
    responsibility for the accuracy, completeness, or usefulness of any information,
    apparatus, product, or process disclosed, or represents that its use would not infringe
    privately-owned rights. Reference herein to any specific commercial products, process,
    or service by trade name, trademark, manufacturer, or otherwise, does not necessarily
    constitute or imply its endorsement, recommendation, or favoring by the United States
    Government. The views and opinions of authors expressed herein do not necessarily state
    or reflect those of the United States Government, and shall not be used for advertising
    or product endorsement purposes.

*********************************************************************************************/

 /********************************************************************
 *
 * Module Name : main.c
 *
 * Author/Date : Jan C. Depner
 *
 * Description : Dumps CHARTS HOF waveforms in a specified area using the specified PFM.
 *
 ********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>


/* Local Includes. */

#include "nvutility.h"

#include "FileHydroOutput.h"
#include "FileWave.h"

#include "version.h"


void usage ()
{
  fprintf (stderr, "\nPurpose: This program dumps HOF waveforms to an ASCII output file.\n");
  fprintf (stderr, "The HOF files are determined by searching a PFM file for HOF records.\n");
  fprintf (stderr, "\tThe records will only be retrieved for the specified area.\n\n");
  fprintf (stderr, "Usage: dump_waveforms -a AREA_FILE -o OUTPUT_FILE [-n] [-d] PFM_FILENAME \n");
  fprintf (stderr, "\nWhere:\n");
  fprintf (stderr, "\t-n  =  Dump all records (even invalid records).\n");
  fprintf (stderr, "\t-d  =  Do not perform orthometric conversion on Z values).\n");
  fprintf (stderr, "\tPFM_FILENAME = Name of a pfm file (.pfm).\n");
  fprintf (stderr, "\tAREA_FILE = Area file name.\n");
  fprintf (stderr, "\t\tThe area file name must have a .ARE extension\n");
  fprintf (stderr, "\t\tfor ISS60 type area files, .are for generic area files or,\n");
  fprintf (stderr, "\t\t.afs for Army Corps area files.\n");
  fprintf (stderr, "\t\tGeneric area files consist of a simple list of\n");
  fprintf (stderr, "\t\tpolygon points.  The points may be in any of the following\n");
  fprintf (stderr, "\t\tformats:\n\n");
  fprintf (stderr, "\t\t\tHemisphere Degrees Minutes Seconds.decimal\n");
  fprintf (stderr, "\t\t\tHemisphere Degrees Minutes.decimal\n");
  fprintf (stderr, "\t\t\tHemisphere Degrees.decimal\n");
  fprintf (stderr, "\t\t\tSign Degrees Minutes Seconds.decimal\n");
  fprintf (stderr, "\t\t\tSign Degrees Minutes.decimal\n");
  fprintf (stderr, "\t\t\tSign Degrees.decimal\n\n");
  fprintf (stderr, "\t\tThe lat and lon must be entered one per line, separated by\n");
  fprintf (stderr, "\t\ta comma.  You do not need to repeat the first point, the\n");
  fprintf (stderr, "\t\tpolygon will be closed automatically.\n\n");
  fprintf (stderr, "\tOUTPUT_FILE = Output file name.\n\n");
  fflush (stderr);
}



/*  This is the sort function for qsort.  */

static int32_t compare_files (const void *a, const void *b)
{
  NV_I32_COORD2 *sa = (NV_I32_COORD2 *)(a);
  NV_I32_COORD2 *sb = (NV_I32_COORD2 *)(b);

  return (sa->x < sb->x ? 0 : 1);
}



int32_t main (int32_t argc, char **argv)
{
  char               area_file[512], out_file[512], wave_file[512], file[4096][512];
  NV_I32_COORD2      *file_rec = NULL;
  float              value;
  BIN_RECORD         bin;
  DEPTH_RECORD       *dep;
  PFM_OPEN_ARGS      open_args;
  double             polygon_x[200], polygon_y[200];
  NV_F64_XYMBR       mbr;
  int32_t            i, j, k, pfm_handle = -1, x_start, y_start, width, height, recnum, list, last, file_rec_count = 0, prev_file,
                     percent = 0, old_percent = -1, polygon_count;
  NV_I32_COORD2      coord;
  FILE               *hfp = NULL, *ofp, *wfp = NULL;
  HYDRO_OUTPUT_T     hof;
  WAVE_DATA_T        wave_data;
  WAVE_HEADER_T      wave_header;
  uint8_t            got_area = NVFalse, got_out = NVFalse, inval = NVFalse, ortho = NVTrue, hit, good = NVFalse;
  int16_t            data_type_lut[4096];
  char               c;
  extern char        *optarg;
  extern int         optind;


  fprintf (stderr, "\n\n %s \n\n\n", VERSION);


  while ((c = getopt (argc, argv, "a:o:nd")) != EOF)
    {
      switch (c)
        {
        case 'a':
          strcpy (area_file, optarg);
          got_area = NVTrue;
          break;

        case 'o':
          strcpy (out_file, optarg);
          got_out = NVTrue;
          break;

        case 'n':
          inval = NVTrue;
          break;

        case 'd':
          ortho = NVFalse;
          break;

        default:
          usage ();
          exit (-1);
          break;
        }
    }


  if (optind >= argc || !got_area || !got_out)
    {
      usage ();
      exit (-1);
    }


  strcpy (open_args.list_path, argv[optind]);

  open_args.checkpoint = 0;
  pfm_handle = open_existing_pfm_file (&open_args);

  if (pfm_handle < 0) pfm_error_exit (pfm_error);


  if (!strstr (area_file, ".are") && !strstr (area_file, ".ARE") && !strstr (area_file, ".afs"))
    {
      fprintf (stderr, "File %s not a known type of area file.\n\n", area_file);
      usage ();
      exit (-1);
    }

  get_area_mbr (area_file, &polygon_count, polygon_x, polygon_y, &mbr);


  if ((ofp = fopen (out_file, "w")) == NULL)
    {
      perror (out_file);
      exit (-1);
    }


  /*  Build a lookup table for the file types and save the file names so we don't have to keep reading them from the .ctl file.  */

  memset (data_type_lut, 0, 4096 * sizeof (int16_t));

  last = get_next_list_file_number (pfm_handle);

  hit = NVFalse;
  for (list = 0 ; list < last ; list++)
    {
      read_list_file (pfm_handle, list, file[list], &data_type_lut[list]);
      if (data_type_lut[list] == PFM_SHOALS_1K_DATA || data_type_lut[list] == PFM_CHARTS_HOF_DATA) hit = NVTrue;
    }


  if (!hit)
    {
      fprintf (stderr, "\n\nNo hydro data available in the input PFM file.\n\n");
      exit (-1);
    }


  x_start = 0;
  y_start = 0;
  width = open_args.head.bin_width;
  height = open_args.head.bin_height;

  if (mbr.min_y > open_args.head.mbr.max_y || mbr.max_y < open_args.head.mbr.min_y ||
      mbr.min_x > open_args.head.mbr.max_x || mbr.max_x < open_args.head.mbr.min_x)
    {
      fprintf (stderr, "\n\nSpecified area is completely outside of the PFM bounds!\n\n");
      exit (-1);
    }


  /*  Match to nearest cell.  */

  x_start = NINT ((mbr.min_x - open_args.head.mbr.min_x) / open_args.head.x_bin_size_degrees);
  y_start = NINT ((mbr.min_y - open_args.head.mbr.min_y) / open_args.head.y_bin_size_degrees);
  width = NINT ((mbr.max_x - mbr.min_x) / open_args.head.x_bin_size_degrees);
  height = NINT ((mbr.max_y - mbr.min_y) / open_args.head.y_bin_size_degrees);


  /*  Adjust to PFM bounds if necessary.  */

  if (x_start < 0) x_start = 0;
  if (y_start < 0) y_start = 0;
  if (x_start + width > open_args.head.bin_width) width = open_args.head.bin_width - x_start;
  if (y_start + height > open_args.head.bin_height) height = open_args.head.bin_height - y_start;


  /*  Redefine bounds.  */

  mbr.min_x = open_args.head.mbr.min_x + x_start * open_args.head.x_bin_size_degrees;
  mbr.min_y = open_args.head.mbr.min_y + y_start * open_args.head.y_bin_size_degrees;
  mbr.max_x = mbr.min_x + width * open_args.head.x_bin_size_degrees;
  mbr.max_y = mbr.min_y + height * open_args.head.y_bin_size_degrees;


  for (i = y_start ; i < y_start + height ; i++)
    {
      coord.y = i;
      for (j = x_start ; j < x_start + width ; j++)
        {
          coord.x = j;

          read_bin_record_index (pfm_handle, coord, &bin);


          if (bin.num_soundings)
            {
              if (!read_depth_array_index (pfm_handle, coord, &dep, &recnum))
                {
                  for (k = 0 ; k < recnum ; k++)
                    {
                      if (!(dep[k].validity & PFM_DELETED))
                        {
                          if (!(dep[k].validity & PFM_INVAL) || inval)
                            {
                              if (data_type_lut[dep[k].file_number] == PFM_SHOALS_1K_DATA || data_type_lut[dep[k].file_number] == PFM_CHARTS_HOF_DATA)
                                {
                                  if (inside_polygon2 (polygon_x, polygon_y, polygon_count, dep[k].xyz.x, dep[k].xyz.y))
                                    {
                                      file_rec = (NV_I32_COORD2 *) realloc (file_rec, (file_rec_count + 1) * sizeof (NV_I32_COORD2));
                                      if (file_rec == NULL)
                                        {
                                          perror ("Allocating file_rec memory in main.c");
                                          exit (-1);
                                        }
                                      file_rec[file_rec_count].x = dep[k].file_number;
                                      file_rec[file_rec_count].y = dep[k].ping_number;
                                      file_rec_count++;
                                    }
                                }
                            }
                        }
                    }

                  free (dep);
                }
            }
        }

      percent = NINT (((float) i / (float) height) * 100.0L);
      if (percent != old_percent)
        {
          fprintf (stderr, "%03d%% read                 \r", percent);
          old_percent = percent;
        }
    }


  fprintf (stderr, "100%% read, sorting data         \r");
  old_percent = -1;


  qsort (file_rec, file_rec_count, sizeof (NV_I32_COORD2), compare_files);


  fprintf (ofp, "#First line - LAT,LON,Z ; 2nd line - 500 PMT values ; 3rd line - 200 APD values ; 4th line - 200 IR values ; 5th line - 80 Raman values ; [Wash, rinse, repeat]\n");

  prev_file = -1;
  for (i = 0 ; i < file_rec_count ; i++)
    {
      if (prev_file != file_rec[i].x)
        {
          if (good)
            {
              fclose (wfp);
              fclose (hfp);
            }


          good = NVFalse;
          hfp = open_hof_file (file[file_rec[i].x]);

          if (hfp != NULL)
            {
              strcpy (wave_file, file[file_rec[i].x]);
              sprintf (&wave_file[strlen (wave_file) - 4], ".inh");

              wfp = open_wave_file (wave_file);

              if (wfp != NULL)
                {
                  wave_read_header (wfp, &wave_header);

                  good = NVTrue;
                }
            }
          prev_file = file_rec[i].x;
        }
      else
        {
          if (good)
            {
              hof_read_record (hfp, file_rec[i].y, &hof);
              wave_read_record (wfp, file_rec[i].y, &wave_data);


              if (ortho)
                {
                  value = get_geoid12b (hof.latitude, hof.longitude);

                  if (value != -999.0) hof.correct_depth -= value;
                }

              fprintf (ofp, "%.9f,%.9f,%f\n", hof.latitude, hof.longitude, hof.correct_depth);


              for (j = 1 ; j < wave_header.pmt_size ; j++)
                {
                  if (j) fprintf (ofp, "%d", wave_data.pmt[j]);
                  if (j != wave_header.pmt_size - 1) fprintf (ofp, ",");
                }
              fprintf (ofp, "\n");

              for (j = 1 ; j < wave_header.apd_size ; j++)
                {
                  if (j) fprintf (ofp, "%d", wave_data.apd[j]);
                  if (j != wave_header.apd_size - 1) fprintf (ofp, ",");
                }
              fprintf (ofp, "\n");

              for (j = 1 ; j < wave_header.ir_size ; j++)
                {
                  if (j) fprintf (ofp, "%d", wave_data.ir[j]);
                  if (j != wave_header.ir_size - 1) fprintf (ofp, ",");
                }
              fprintf (ofp, "\n");

              for (j = 1 ; j < wave_header.raman_size ; j++)
                {
                  if (j) fprintf (ofp, "%d", wave_data.raman[j]);
                  if (j != wave_header.raman_size - 1) fprintf (ofp, ",");
                }
              fprintf (ofp, "\n");
            }
        }

      percent = NINT (((float) i / (float) file_rec_count) * 100.0L);
      if (percent != old_percent)
        {
          fprintf (stderr, "%03d%% written            \r", percent);
          old_percent = percent;
        }
    }


  fprintf (stderr, "%d waveform records written         \n\n", file_rec_count);


  free (file_rec);

  if (good )
    {
      fclose (wfp);
      fclose (hfp);
    }

  fclose (ofp);


  return (0);
}
