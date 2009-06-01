#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#include <config.h>
#ifdef HAVE_DV

#include <XVDig1394.h>
#include <XVMacros.h>

using namespace std ;

extern int debug;

namespace {
  typedef enum{ YUV444,YUV411,YUV422,RGB8, MONO8, MONO16} ModeDescr;

  static struct{
    int width, height;
    ModeDescr mode;
    } mode_descr[]=
    {{160,120,YUV444},{320,240,YUV422},{640,480,YUV411},
     {640,480,YUV422},{640,480,RGB8},{640,480,MONO8},
     {640,480,MONO16},{800,600,YUV422},{800,600,RGB8},
     {800,600,MONO8},{1024,768,YUV422},{1024,768,RGB8},
     {1024,768,MONO8},{800,600,MONO16},{1024,768,MONO16},
     {1280,960,YUV422},{1280,960,RGB8},{1280,960,MONO8},
     {1600,1200,YUV422},{1600,1200,RGB8},{1600,1200,MONO8},
     {1280,960,MONO16},{1600,1200,MONO16}};
    
  // manually initialize the YUB<->RGB conversion tables since we are going
  // to use these tables directly.
  void init_tab(void) { 
    if( ! bRGB2YUVTableBuilt ) {
      buildRGB2YUVTable();
    }
    if( ! bYUV2RGBTableBuilt ) {
      buildYUV2RGBTable();
    }
  }; // init_tab

  // helper class to help conversion functions
  template<class IMTYPE>
  struct Convert {
    typedef typename IMTYPE::PIXELTYPE PIXELTYPE ;

  // convert a buffer of YUV444 image into an IMTYPE (assume XVImageRGB)
  // YUV8 4:4:4 format:  U Y V, so buffer size = # pixels * 3
    static
  void yuv444_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*3 ;
    unsigned char pg ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 3, ++ iTarg ) {
      pg =  TABLE_YUV_TO_PG[ ptr[0] ][ ptr[2] ] ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[1] ][ ptr[2] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[1] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[1] ][ pg ] );
    }
  } // yuv444_f

  // convert a buffer of YUV422 image into an IMTYPE (assume XVImageRGB)
  // YUV8 4:2:2 format:  U Y1 V Y2, so buffer size = # pixels * / 2 * 4
    static
  void yuv422_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*2 ;
    unsigned char pg ;
#ifdef HAVE_IPP
    IppiSize roi={targ.Width(),targ.Height()};
    XVImageRGB<XV_RGB24> temp;
    const int dstOrder[4]={2,1,0,3};
    switch(targ.ImageType())
    {
       case XVImage_RGB24:
         temp.resize(targ.Width(),targ.Height());
	 ippiYCbCr422ToCbYCr422_8u_C2R((const Ipp8u *)ptr,targ.Width()*2,
	 			   (Ipp8u*)ptr,targ.Width()*2,roi);
         ippiYCbCr422ToRGB_8u_C2C3R((const Ipp8u *)ptr,targ.Width()*2,
	                          (Ipp8u *)temp.lock(),
				  temp.SizeX()*sizeof(XV_RGB24),roi);
	 ippiSwapChannels_8u_C3R((const Ipp8u*)temp.data(),
	                             targ.Width()*sizeof(XV_RGB24),
				     (Ipp8u *)targ.lock(),
				     targ.SizeX()*sizeof(XV_RGB24),
				     roi,dstOrder);
	 targ.unlock();
	 return;
      case XVImage_RGB32:
         temp.resize(targ.Width(),targ.Height());
	 ippiYCbCr422ToCbYCr422_8u_C2R((const Ipp8u *)ptr,targ.Width()*2,
	 			   (Ipp8u*)ptr,targ.Width()*2,roi);
         ippiYCbCr422ToRGB_8u_C2C3R((const Ipp8u *)ptr,targ.Width()*2,
	                          (Ipp8u *)temp.lock(),
				  temp.SizeX()*sizeof(XV_RGB24),roi);
	 temp.unlock();
	 ippiSwapChannels_8u_C3C4R((const Ipp8u*)temp.data(),
	                             targ.Width()*sizeof(XV_RGB24),
				     (Ipp8u *)targ.lock(),
				     targ.SizeX()*sizeof(XV_RGBA32),
				     roi,dstOrder,0);
	 targ.unlock();
         return;
      default: // do it the old way?
         break;
         
    }
#endif
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 4 ) {
      pg =  TABLE_YUV_TO_PG[ ptr[0] ][ ptr[2] ] ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[1] ][ ptr[2] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[1] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[1] ][ pg ] );
      ++ iTarg ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[3] ][ ptr[2] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[3] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[3] ][ pg ] );
      ++ iTarg ;
    }
  } // yuv422_f

  // convert a buffer of YUV411 image into an IMTYPE (assume XVImageRGB)
  // YUV8 4:1:1 format:  U Y1 Y2 V Y3 Y4, so buffer size = # pixels / 4 * 6
    static 
  void yuv411_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*3/2 ;
    unsigned char pg ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 6 ) {
      pg =  TABLE_YUV_TO_PG[ ptr[0] ][ ptr[3] ] ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[1] ][ ptr[3] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[1] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[1] ][ pg ] );
      ++ iTarg ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[2] ][ ptr[3] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[2] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[2] ][ pg ] );
      ++ iTarg ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[4] ][ ptr[3] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[4] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[4] ][ pg ] );
      ++ iTarg ;
      iTarg -> setR( TABLE_YUV_TO_R[ ptr[5] ][ ptr[3] ] );
      iTarg -> setB( TABLE_YUV_TO_B[ ptr[5] ][ ptr[0] ] );
      iTarg -> setG( TABLE_YUV_TO_G[ ptr[5] ][ pg ] );
      ++ iTarg ;
    }
  } // yuv411_f

  // convert a buffer of RGB image into an IMTYPE (assume XVImageRGB)
  // RGB8 format:  R G B, so buffer size = # pixels * 3
    static
  void rgb8_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*3 ;
#ifdef HAVE_IPP
     const int dstOrder[4]={2,1,0,3};
     IppiSize roi={targ.Width(),targ.Height()};
     switch(targ.ImageType())
     {
	  case XVImage_RGB24:
	   ippiSwapChannels_8u_C3R((const Ipp8u*)ptr,
	                             targ.Width()*sizeof(XV_RGB24),
				     (Ipp8u *)targ.lock(),
				     targ.SizeX()*sizeof(XV_RGB24),
				     roi,dstOrder);
	    targ.unlock();
	    return;
          case XVImage_RGB32:
	   ippiSwapChannels_8u_C3C4R((const Ipp8u*)ptr,
	                             targ.Width()*sizeof(XV_RGB24),
				     (Ipp8u *)targ.lock(),
				     targ.SizeX()*sizeof(XV_RGBA32),
				     roi,dstOrder,0);
	    targ.unlock();
	    return;
	  default:
	    break;
      }
#endif
   XVImageWIterator<PIXELTYPE>  iTarg(targ);
   for ( ; ptr < ptrEnd ; ++ iTarg ) {
      iTarg -> setR( *ptr++ );
      iTarg -> setG( *ptr++ );
      iTarg -> setB( *ptr++ );
   }
  } // rgb8_f

  // convert a buffer of RGB image into an IMTYPE (assume XVImageRGB)
  // RGB16 format:  Rh Rl Gh Gl Bh Bl, so buffer size = # pixels * 6
    static
  void rgb16_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*6 ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ++ iTarg ) {
      iTarg -> setR( *ptr );
      ptr += 2 ;
      iTarg -> setG( *ptr );
      ptr += 2 ;
      iTarg -> setB( *ptr );
      ptr += 2 ;
    }
  } // rgb16_f

  // convert a buffer of 8 bit mono image into an IMTYPE (assume XVImageRGB)
    static
  void mono8_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr ++, ++ iTarg ) {
      iTarg -> setR( *ptr );
      iTarg -> setG( *ptr );
      iTarg -> setB( *ptr );
    }
  } // mono8_f

  // convert a buffer of 16 bit mono image into an IMTYPE (assume XVImageRGB)
    static
  void mono16_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels * 2 ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 2, ++ iTarg ) {
      iTarg -> setR( *ptr );
      iTarg -> setG( *ptr );
      iTarg -> setB( *ptr );
    }
  } // mono16_f

  }; // struct Convert

  // partial specialization of Convert to deal with XVImageYUV
  template<class PIXELTYPE>
  struct Convert<XVImageYUV<PIXELTYPE> > {
    typedef XVImageYUV<PIXELTYPE> IMTYPE ;

  // convert a buffer of YUV444 image into a XVImageYUV (independent Y,U and V)
  // YUV8 4:4:4 format:  U Y V, so buffer size = # pixels * 3
    static
  void yuv444_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*3 ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ++ iTarg ) {
      iTarg -> setU( *ptr++ );
      iTarg -> setY( *ptr++ );
      iTarg -> setV( *ptr++ );
    }
  } // yuv444_f<XVImageYUV>

  // convert a buffer of YUV422 image into a XVImageYUV (independent Y,U and V)
  // YUV8 4:2:2 format:  U Y1 V Y2, so buffer size = # pixels * / 2 * 4
    static
  void yuv422_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*2 ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 4 ) {
      iTarg -> setU( ptr[0] );
      iTarg -> setY( ptr[1] );
      iTarg -> setV( ptr[2] );
      ++ iTarg ;
      iTarg -> setU( ptr[0] );
      iTarg -> setY( ptr[3] );
      iTarg -> setV( ptr[2] );
      ++ iTarg ;
    }
  } // yuv422_f<XVImageYUV>

  // convert a buffer of YUV411 image into a XVImageYUV (independent Y,U and V)
  // YUV8 4:1:1 format:  U Y1 Y2 V Y3 Y4, so buffer size = # pixels / 4 * 6
    static
  void yuv411_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*3/2 ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 6 ) {
      iTarg -> setU( ptr[0] );
      iTarg -> setY( ptr[1] );
      iTarg -> setV( ptr[3] );
      ++ iTarg ;
      iTarg -> setU( ptr[0] );
      iTarg -> setY( ptr[2] );
      iTarg -> setV( ptr[3] );
      ++ iTarg ;
      iTarg -> setU( ptr[0] );
      iTarg -> setY( ptr[4] );
      iTarg -> setV( ptr[3] );
      ++ iTarg ;
      iTarg -> setU( ptr[0] );
      iTarg -> setY( ptr[5] );
      iTarg -> setV( ptr[3] );
      ++ iTarg ;
    }
  } // yuv411_f<XVImageYUV>

  // convert a buffer of RGB image into a XVImageYUV (independent Y,U and V)
  // RGB8 format:  R G B, so buffer size = # pixels * 3
    static
  void rgb8_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*3 ;
    unsigned char py, y ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for( ; ptr < ptrEnd ; ptr += 3, ++ iTarg ) {
      py = TABLE_RGB_TO_PY[ ptr[0] ][ ptr[1] ];
      y = TABLE_RGB_TO_Y[ py ][ ptr[2] ];
      iTarg -> setU( TABLE_RGB_TO_U[ ptr[2] ][ y ] );
      iTarg -> setY( y );
      iTarg -> setV( TABLE_RGB_TO_U[ ptr[0] ][ y ] );
    }
  } // rgb8_f<XVImageYUV>

  // convert a buffer of RGB image into a XVImageYUV (independent Y,U and V)
  // RGB16 format:  Rh Rl Gh Gl Bh Bl, so buffer size = # pixels * 6
    static
  void rgb16_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels*6 ;
    unsigned char py, y ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for( ; ptr < ptrEnd ; ptr += 6, ++ iTarg ) {
      py = TABLE_RGB_TO_PY[ ptr[0] ][ ptr[2] ];
      y = TABLE_RGB_TO_Y[ py ][ ptr[4] ];
      iTarg -> setU( TABLE_RGB_TO_U[ ptr[4] ][ y ] );
      iTarg -> setY( y );
      iTarg -> setV( TABLE_RGB_TO_U[ ptr[0] ][ y ] );
    }
  } // rgb16_f<XVImageYUV>

  // convert a buffer of 8 bit mono image into a XVImageYUV (i11t Y,U and V)
    static
  void mono8_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr ++, ++ iTarg ) {
      iTarg -> setY( *ptr );
      iTarg -> setU( 0 );
      iTarg -> setV( 0 );
    }
  } // mono8_f<XVImageYUV>

  // convert a buffer of 16 bit mono image into a XVImageYUV (i11t Y,U and V)
    static
  void mono16_f( const unsigned char * buf, IMTYPE& targ, int numPixels ) {
    const unsigned char * ptr = buf, * ptrEnd = buf + numPixels * 2 ;
    XVImageWIterator<PIXELTYPE>  iTarg(targ);
    for ( ; ptr < ptrEnd ; ptr += 2, ++ iTarg ) {
      iTarg -> setY( *ptr );
      iTarg -> setU( 0 );
      iTarg -> setV( 0 );
    }
  } // mono16_f<XVImageYUV>

  }; // struct Convert<XVImageYUV>

  enum Color { mono8, yuv411, yuv422, yuv444, rgb8, mono16, rgb16 } ;
  const char * strColor[] = 
    { "Mono8", "YUV8 4:1:1", "YUV8 4:2:2", "YUV8 4:4:4", 
      "RGB8", "Mono16", "RGB16" } ;

  // returns the correpsonding color-space conversion function
  template<class T>
  void (*color_f( Color c ))( const unsigned char *, T&, int ) {
    switch( c ) {
    case mono8 :
      return &Convert<T>::mono8_f ;
    case mono16:
      return &Convert<T>::mono16_f ;
    case rgb8 :
      return &Convert<T>::rgb8_f ;
    case rgb16 :
      return &Convert<T>::rgb16_f ;
    case yuv411:
      return &Convert<T>::yuv411_f ;
    case yuv422:
      return &Convert<T>::yuv422_f ;
    case yuv444:
      return &Convert<T>::yuv444_f ;
    default:
      return 0 ; // invalid or unimplemented
    }
  }

  // returns the size of a line of pixels
  int getLineSize( int width,  Color c ) {
    switch( c ) {
    case mono8 :
      return width ;
    case mono16 :
      return width * 2 ;
    case rgb8 :
      return width * 3 ;
    case rgb16 :
      return width * 6 ;
    case yuv411:
      return width * 3 / 2 ;
    case yuv422:
      return width * 2 ;
    case yuv444:
      return width * 3 ;
    default:
      return 0 ; // invalid or unimplemented
    }
  }

  // returns the size of required buffer
  inline int getBufSize( int width, int height, Color c ) {
    return getLineSize( width, c ) * height ;
  }

  const char * strFrameRates[] = 
    { "1.875", "3.75", "7.5", "15", "30", "60" };

  }
  /* note: the logic of the code artificially limits the selection of 
     pixel color encoding and image size (scale) in format 7 into 
     those avaible in format 0-2 because these information
     is still carried in format and mode data members from
     set_params() to the class constructor.
   */

  /* The following code is to deal with the the change of prototype of 
     dc1394_query_format7_total_bytes between different versions of 
     dc1394_control.h . The fourth parameter has been changed from 
     unsigned int * to unsigned long long int * . 
     Ideally this should be resolved in autoconf level instead of .cc level
     however I (Donald) don't have time to investigate which version is which
     so here is just a quick dirty fix using template partial specialization.
   */
  

template <class IMTYPE>
int XVDig1394<IMTYPE>::initiate_acquire(int i_frame)
{
   if(i_frame<0 || i_frame>=n_buffers) return 0;
   pthread_mutex_lock(&wait_grab[i_frame]);
   requests.push_front(i_frame);
   return 1;
}


template <class PIXELTYPE> 
void    BayerNearestNeighbor(unsigned char *src, 
                 XVImageRGB<PIXELTYPE> & targ, int sx, int sy,
		 dc1394color_filter_t optical_filter)
{
  PIXELTYPE  *data=targ.lock();
  register int i,j;

  switch (optical_filter) {
  case DC1394_COLOR_FILTER_GRBG: //-------------------------------------------
    // copy original RGB data to output images
    for (i=0;i<sy;i+=2) {
      for (j=0;j<sx;j+=2) {
	data[(i*sx+j)].setG(src[i*sx+j]);
	data[((i+1)*sx+(j+1))].setG(src[(i+1)*sx+(j+1)]);
	data[(i*sx+j+1)].setB(src[i*sx+j+1]);
	data[((i+1)*sx+j)].setR(src[(i+1)*sx+j]);
      }
    }
    // B channel
    for (i=0;i<sy;i+=2) {
      for (j=0;j<sx-1;j+=2) {
	data[(i*sx+j)].setB(data[(i*sx+j+1)].B());
	data[((i+1)*sx+j+1)].setB(data[(i*sx+j+1)].B());
	data[((i+1)*sx+j)].setB(data[(i*sx+j+1)].B());
      }
    }
      // R channel
    for (i=0;i<sy-1;i+=2)  { //every two lines
      for (j=0;j<sx-1;j+=2) {
	data[(i*sx+j)].setR(data[((i+1)*sx+j)].R());
	data[(i*sx+j+1)].setR(data[((i+1)*sx+j)].R());
	data[((i+1)*sx+j+1)].setR(data[((i+1)*sx+j)].R());
      }
    }
    // using lower direction for G channel
      
    // G channel
    for (i=0;i<sy-1;i+=2)//every two lines
      for (j=1;j<sx;j+=2)
	data[(i*sx+j)].setG(data[((i+1)*sx+j)].G());
      
    for (i=1;i<sy-2;i+=2)//every two lines
      for (j=0;j<sx-1;j+=2)
	data[(i*sx+j)].setG(data[((i+1)*sx+j)].G());
    
    // copy it for the next line
    for (j=0;j<sx-1;j+=2)
      data[((sy-1)*sx+j)].setG(data[((sy-2)*sx+j)].G());
    
    break;

  case DC1394_COLOR_FILTER_GBRG:
    // copy original RGB data to output images
    for (i=0;i<sy;i+=2) {
      for (j=0;j<sx;j+=2) {
	data[(i*sx+j)].setG(src[i*sx+j]);
	data[((i+1)*sx+(j+1))].setG(src[(i+1)*sx+(j+1)]);
	data[(i*sx+j+1)].setR(src[i*sx+j+1]);
	data[((i+1)*sx+j)].setB(src[(i+1)*sx+j]);
      }
    }
    // R channel
    for (i=0;i<sy;i+=2) {
      for (j=0;j<sx-1;j+=2) {
	data[(i*sx+j)].setR(data[(i*sx+j+1)].R());
	data[((i+1)*sx+j+1)].setR(data[(i*sx+j+1)].R());
	data[((i+1)*sx+j)].setR(data[(i*sx+j+1)].R());
      }
    }
      // B channel
    for (i=0;i<sy-1;i+=2)  { //every two lines
      for (j=0;j<sx-1;j+=2) {
	data[(i*sx+j)].setB(data[((i+1)*sx+j)].B());
	data[(i*sx+j+1)].setB(data[((i+1)*sx+j)].B());
	data[((i+1)*sx+j+1)].setB(data[((i+1)*sx+j)].B());
      }
    }
    // using lower direction for G channel
      
    // G channel
    for (i=0;i<sy-1;i+=2)//every two lines
      for (j=1;j<sx;j+=2)
	data[(i*sx+j)].setG(data[((i+1)*sx+j)].G());
      
    for (i=1;i<sy-2;i+=2)//every two lines
      for (j=0;j<sx-1;j+=2)
	data[(i*sx+j)].setG(data[((i+1)*sx+j)].G());
    
    // copy it for the next line
    for (j=0;j<sx-1;j+=2)
      data[((sy-1)*sx+j)].setG(data[((sy-2)*sx+j)].G());
    
    break;
  case DC1394_COLOR_FILTER_RGGB:
    // copy original data
    for (i=0;i<sy;i+=2) {
      for (j=0;j<sx;j+=2) {
	data[(i*sx+j)].setR(src[i*sx+j]);
	data[((i+1)*sx+(j+1))].setB(src[(i+1)*sx+(j+1)]);
	data[(i*sx+j+1)].setG(src[i*sx+j+1]);
	data[((i+1)*sx+j)].setG(src[(i+1)*sx+j]);
      }
    }
    // B channel
    for (i=0;i<sy;i+=2){
      for (j=0;j<sx-1;j+=2) {
	data[(i*sx+j)].setB(data[((i+1)*sx+j+1)].B());
	data[(i*sx+j+1)].setB(data[((i+1)*sx+j+1)].B());
	data[((i+1)*sx+j)].setB(data[((i+1)*sx+j+1)].B());
      }
    }
    // R channel
    for (i=0;i<sy-1;i+=2) { //every two lines
      for (j=0;j<sx-1;j+=2) {
	data[((i+1)*sx+j)].setR(data[(i*sx+j)].R());
	data[(i*sx+j+1)].setR(data[(i*sx+j)].R());
	data[((i+1)*sx+j+1)].setR(data[(i*sx+j)].R());
      }
    }
    // using lower direction for G channel
    
    // G channel
    for (i=0;i<sy-1;i+=2)//every two lines
      for (j=0;j<sx-1;j+=2)
	data[(i*sx+j)].setG(data[((i+1)*sx+j)].G());
    
    for (i=1;i<sy-2;i+=2)//every two lines
      for (j=0;j<sx-1;j+=2)
	data[(i*sx+j+1)].setG(data[((i+1)*sx+j+1)].G());
    
    // copy it for the next line
    for (j=0;j<sx-1;j+=2)
      data[((sy-1)*sx+j+1)].setG(data[((sy-2)*sx+j+1)].G());
    
    break;
    
  case DC1394_COLOR_FILTER_BGGR: //-------------------------------------------
    // copy original data
    for (i=0;i<sy;i+=2) {
      for (j=0;j<sx;j+=2) {
	data[(i*sx+j)].setB(src[i*sx+j]);
	data[((i+1)*sx+(j+1))].setR(src[(i+1)*sx+(j+1)]);
	data[(i*sx+j+1)].setG(src[i*sx+j+1]);
	data[((i+1)*sx+j)].setG(src[(i+1)*sx+j]);
      }
    }
    // R channel
    for (i=0;i<sy;i+=2){
      for (j=0;j<sx-1;j+=2) {
	data[(i*sx+j)].setR(data[((i+1)*sx+j+1)].R());
	data[(i*sx+j+1)].setR(data[((i+1)*sx+j+1)].R());
	data[((i+1)*sx+j)].setR(data[((i+1)*sx+j+1)].R());
      }
    }
    // B channel
    for (i=0;i<sy-1;i+=2) { //every two lines
      for (j=0;j<sx-1;j+=2) {
	data[((i+1)*sx+j)].setB(data[(i*sx+j)].B());
	data[(i*sx+j+1)].setB(data[(i*sx+j)].B());
	data[((i+1)*sx+j+1)].setB(data[(i*sx+j)].B());
      }
    }
    // using lower direction for G channel
    
    // G channel
    for (i=0;i<sy-1;i+=2)//every two lines
      for (j=0;j<sx-1;j+=2)
	data[(i*sx+j)].setG(data[((i+1)*sx+j)].G());
    
    for (i=1;i<sy-2;i+=2)//every two lines
      for (j=0;j<sx-1;j+=2)
	data[(i*sx+j+1)].setG(data[((i+1)*sx+j+1)].G());
    
    // copy it for the next line
    for (j=0;j<sx-1;j+=2)
      data[((sy-1)*sx+j+1)].setG(data[((sy-2)*sx+j+1)].G());
    
    break;
    
  default:  //-------------------------------------------
    break;
  }
  targ.unlock();
}

#ifdef HAVE_IPP
void BayerNearestNeighbor(unsigned char *src, 
        XVImageRGB<XV_RGB24>& targ, int sx, int sy,dc1394color_filter_t
	optical_filter)
{
   IppiBayerGrid grid;

   switch(optical_filter)
   {
      case DC1394_COLOR_FILTER_GRBG:
        grid=ippiBayerGRBG;
	break;
      case DC1394_COLOR_FILTER_BGGR:
        grid=ippiBayerBGGR;
	break;
      case DC1394_COLOR_FILTER_GBRG:
        grid=ippiBayerGBRG;
	break;
      default:
        cerr << "Unknown filter defaulting to RGGB" <<endl;
      case DC1394_COLOR_FILTER_RGGB:
        grid=ippiBayerRGGB;
	break;
   }

   IppiRect roi={0,0,sx-1,sy-1};
   IppiSize imsize={sx,sy};
   ippiCFAToRGB_8u_C1C3R((const Ipp8u *) src,roi,imsize,
           sx,(Ipp8u *) targ.lock(),targ.Width()*sizeof(XV_RGB24),
	   grid,0);
}

void BayerNearestNeighbor(unsigned char *src, 
        XVImageRGB<XV_RGBA32>& targ, int sx, int sy,dc1394color_filter_t
	optical_filter)
{
   XVImageRGB<XV_RGB24> tmpimg(targ.Width(),targ.Height());
   BayerNearestNeighbor(src,tmpimg,sx,sy,optical_filter);
   IppiSize roi={tmpimg.Width(),tmpimg.Height()};
   int dstOrder[4]={0,1,2,3};
   ippiSwapChannels_8u_C3C4R((const Ipp8u*) tmpimg.data(),
        tmpimg.Width()*3,(Ipp8u*)targ.lock(), 
        targ.Width()*sizeof(XV_RGBA32),roi,dstOrder,0);
   targ.unlock();
}
#endif
template <class PIXELTYPE> 
void BayerNearestNeighbor(unsigned char *src, 
        XVImageYUV<PIXELTYPE>& targ, int sx, int sy,dc1394color_filter_t
	optical_filter){cerr << "Bayer decoder does not support YUV"<<
	endl;};

template <class IMTYPE>
int XVDig1394<IMTYPE>::wait_for_completion(int i_frame)
{
           
   pthread_mutex_lock(&wait_grab[i_frame]);
   pthread_mutex_unlock(&wait_grab[i_frame]);
   return 1;
}

template <class IMTYPE>
int XVDig1394<IMTYPE>::set_params(char *paramstring) {

   XVParser	parse_result;  

   bool direct = false ;
   n_buffers = DIG_DEF_NUMFRAMES;
   while( parse_param(paramstring,parse_result) > 0 ) {
     switch(parse_result.c) {
     case 'B':
       n_buffers = parse_result.val;
       break;
     case 'R':
       rgb_pixel = parse_result.val;
       direct = false ;
       break;
     case 'V':
       verbose = (parse_result.val > 0 ? true:false);
       break;
     case 'S':
       scale = parse_result.val+1;
       direct = false ;
       break;
     case 'M':
       scale = parse_result.val;
       direct = false ;
       break;
     case 'C':
       if( ( grab_center = parse_result.val ) ) {
	 format7 = true ;
       }
       break;
     case 'T':
       if( parse_result.val > 0 && parse_result.val <=
       DC1394_TRIGGER_MODE_NUM ) {
	 ext_trigger = parse_result.val + DC1394_TRIGGER_MODE_0 - 1 ;
       }else {
	 ext_trigger = parse_result.val ;
       }
       break;
     case 'f':
       if( parse_result.val == 7 ) {
	 format7 = true ;
       }else {
	 format = parse_result.val;
	 direct = true ;
       }
       break ;
     case 'm' :
       mode = parse_result.val;
       direct = true ;
       break ;
     case 'r' :
       framerate = parse_result.val;
       break ;
     case 'g':			// gain of the camera
       gain = parse_result.val;
       break;
     case 'u':			// u component for the white balance
       uv[0] = parse_result.val;
       break;
     case 'v':			// v component for the white balance
       uv[1] = parse_result.val;
       break;
     case 'i':			// reset bus
       reset_ieee = parse_result.val;
       break;
     case 's':			// saturation
       saturation = parse_result.val;
       break;
     case 'A':                        // sharpness
       sharpness = parse_result.val ;
       break;
     case 'h' :         //shutter
       shutter = parse_result.val;
       break;
     case 'a' :
       gamma = parse_result.val;
       break;
     case 'x' :
       exposure = parse_result.val;
       break;
     case 'o':   // optical filter selection
       optical_filter = (dc1394color_filter_t)
                 (parse_result.val+DC1394_COLOR_FILTER_RGGB);
       optical_flag=true;
       break;
     default:
       if (verbose) cerr << parse_result.c << "=" << parse_result.val 
	    << " is not supported by XVDig1394 (skipping)" << endl;
       break;
     }
   }
   return 1;
}

template <class IMTYPE>
void XVDig1394<IMTYPE>::close(void)
{
  if(camera_node)
  {
    dc1394_video_set_transmission(camera_node,DC1394_OFF);
    dc1394_capture_stop(camera_node);
    dc1394_camera_free(camera_node);
  }
  if(fd) dc1394_free(fd);
}

template <class IMTYPE> int XVDig1394<IMTYPE>::open(const char *dev_name) {
  // first, empty DMA FiFo buffer
        
  dc1394_video_set_transmission(camera_node,DC1394_ON);
  return 1;
}

template <class IMTYPE>
void *XVDig1394<IMTYPE>::grab_thread(void *obj) {
   
 XVDig1394<IMTYPE> *me=reinterpret_cast<XVDig1394<IMTYPE>*>(obj);
 int i_frame;

 dc1394video_frame_t *cur_frame;
 usleep(200);
 while(1)
 {
  dc1394_capture_dequeue( me->camera_node, DC1394_CAPTURE_POLICY_WAIT,
                   &cur_frame);
  if(!me->requests.empty())
  {
   i_frame=me->requests.back();
   me->requests.pop_back();
   if(!me->format7)
   {
     switch(me->mode_type)
     {
      case RGB8:
        Convert<IMTYPE>::rgb8_f(cur_frame->image,me->frame(i_frame),
				me->frame(i_frame).Width()*
				me->frame(i_frame).Height());
       break;
      case YUV411:
        Convert<IMTYPE>::yuv411_f(cur_frame->image,me->frame(i_frame),
				me->frame(i_frame).Width()*
				me->frame(i_frame).Height());
        break;
      case YUV422:
        Convert<IMTYPE>::yuv422_f(cur_frame->image,me->frame(i_frame),
				me->frame(i_frame).Width()*
				me->frame(i_frame).Height());
        break;
      case YUV444:
        Convert<IMTYPE>::yuv444_f(cur_frame->image,me->frame(i_frame),
				me->frame(i_frame).Width()*
				me->frame(i_frame).Height());
        break;
      default:
        cerr << "unknown" << endl;
        break;
     }
   }
   else
     BayerNearestNeighbor(cur_frame->image,me->frame(i_frame),
     		me->frame(i_frame).Width(),me->frame(i_frame).Height(),
		me->optical_filter);
   
   pthread_mutex_unlock(&me->wait_grab[i_frame]);
  }
  dc1394_capture_enqueue( me->camera_node, cur_frame );

 }
 return NULL;
}

template <class IMTYPE>
XVDig1394<IMTYPE>::~XVDig1394()
{
  if(threadded) {
    pthread_join(grabber_thread,0);
    for(int i=0;i<n_buffers;i++)
      pthread_mutex_destroy(&wait_grab[i]);
  }
  close();
}

template <class IMTYPE>
XVDig1394<IMTYPE>::XVDig1394( const char *dev_name, const char *parm_string,
			      unsigned long long camera_id)
  : XVVideo<IMTYPE>(dev_name,parm_string) {

  dc1394camera_list_t * list;
  dc1394switch_t bOn;
  uint32_t       num_cameras;
  dc1394camera_t **camera;
  threadded=false;

  // parsing parameters
  verbose=true;optical_flag=false;reset_ieee=false;
  format = 0 ; mode = 3 ; framerate = 1 ; scale=1;
  format7 = false ; grab_center = false ; ext_trigger = 0 ;
  gain = -1 ; saturation = -1 ; uv[0] = uv[1] = -1 ; 
  sharpness = -1 ; shutter = -1 ; gamma = -1 ; exposure = -1 ;
  camera_node=NULL;rgb_pixel=-1;
  if(parm_string) set_params((char *)parm_string);

  if(fd = dc1394_new ())
  {
    if(dc1394_camera_enumerate (fd, &list)!=DC1394_SUCCESS || list->num == 0)
    {
      if(verbose)
        cerr << "Couldn't enumerate cameras" << endl;
      throw(1);
    }
  }
  else
    throw(2);
  num_cameras=list->num;
  if(!camera_id) camera_node=dc1394_camera_new(fd,list->ids[0].guid),channel=0;
  else if(~camera_id<num_cameras)
     camera_node=dc1394_camera_new(fd,list->ids[~camera_id].guid),
     		 channel=~camera_id;
  else 
    for(int i=0;i<num_cameras;i++)
    {
      if(verbose) cout << "Found camera ID 0x"<< hex
             << list->ids[i].guid << endl;
      if(camera_id && camera_id==list->ids[i].guid)    
             camera_node=dc1394_camera_new(fd,list->ids[i].guid), channel=i;
    }
  dc1394_camera_free_list(list);
  if(!camera_node)
  {
    if (verbose) cerr << "Could not find camera id 0x" << hex << camera_id << endl;
    throw(2);
  }
  if(verbose) dc1394_camera_print_info(camera_node,stdout);
re_init:
  if(reset_ieee) dc1394_camera_reset(camera_node);
    if(dc1394_video_get_transmission(camera_node,&bOn) != DC1394_SUCCESS)
    {
        if (verbose) 
	   cerr << "XVision2: Unable to query transmission mode" << endl;
      throw(3);
    }
    if(bOn == DC1394_ON)
        if(dc1394_video_set_transmission(camera_node,DC1394_OFF) !=
							DC1394_SUCCESS)
        {
 	  if (verbose) 
	     cerr << "XVision2: Unable to stop iso transmission" << endl;
	  throw(4);
        }
    if(dc1394_video_set_iso_speed( camera_node, DC1394_ISO_SPEED_400)!=
    	DC1394_SUCCESS)
    {
      if(verbose)
        cerr << "Could not read iso speed" << endl;
	throw(5);
    }
  if(!format7)
  {
    dc1394video_modes_t modes;
    dc1394_video_get_supported_modes(camera_node,&modes);
    int mode_index=modes.num-1;
    if(rgb_pixel>-1)
    {
      if(rgb_pixel)
      {
         while(mode_index>=0 && scale)
	 {
	    if(modes.modes[mode_index]<DC1394_VIDEO_MODE_FORMAT7_0&&
	       mode_descr[modes.modes[mode_index]-
	        DC1394_VIDEO_MODE_160x120_YUV444].mode==RGB8) scale--;
	    if(scale) mode_index--; 
	 }
	 if(mode_index<0)
	 {
	   if(verbose) cerr <<"Couldn't find valid RGB mode" << endl;
	   throw(6);
	 }
      }
      else
      {
         while(mode_index>=0 && scale)
	 {
	    if(modes.modes[mode_index]<DC1394_VIDEO_MODE_FORMAT7_0&&
	       mode_descr[modes.modes[mode_index]-
	         DC1394_VIDEO_MODE_160x120_YUV444].mode==YUV422) scale--;
	    if(scale)mode_index--;
	 }
	 if(mode_index<0)
	 {
	   if(verbose) cerr <<"Couldn't find valid YUV mode" << endl;
	   throw(6);
	 }
      }
    }
    else
    {
       int width=0;
       while(mode_index>=0 && scale)
       {
            if(modes.modes[mode_index]<DC1394_VIDEO_MODE_FORMAT7_0 &&
	       mode_descr[modes.modes[mode_index]-
	         DC1394_VIDEO_MODE_160x120_YUV444].mode!=MONO8 &&
	       mode_descr[modes.modes[mode_index]-
	         DC1394_VIDEO_MODE_160x120_YUV444].mode!=MONO16 &&
	       mode_descr[modes.modes[mode_index]-
	          DC1394_VIDEO_MODE_160x120_YUV444].width!=width)
	       scale--,width=mode_descr[modes.modes[mode_index]-
	          DC1394_VIDEO_MODE_160x120_YUV444].width;
	    if(scale) mode_index--;
       }
       if(mode_index<0)
       {
	   if(verbose) cerr <<"Couldn't find valid mode" << endl;
	   throw(6);
       }
    }
    dc1394_video_set_mode(camera_node,modes.modes[mode_index]);
    mode_type=mode_descr[modes.modes[mode_index]-
    			DC1394_VIDEO_MODE_160x120_YUV444].mode;
    dc1394framerates_t rates;
    dc1394_video_get_supported_framerates(camera_node,
                   modes.modes[mode_index],&rates);
    if(rates.num==0) 
    {
      if(verbose)
        cerr << "No possible rates" << endl;
	throw(7);
    }
    int rate_index=rates.num-1;
    framerate--;
    while(rate_index>=0 && framerate) rate_index--,framerate--;
    if(rate_index<0)
    {
      if(verbose)
        cerr << "Could not set rate" << endl;
	throw(7);
    }
    dc1394_video_set_framerate(camera_node,rates.framerates[rate_index]);
    if(dc1394_video_set_iso_channel(camera_node,channel)!=DC1394_SUCCESS)
    {
       reset_ieee=true;
       goto re_init;
    }
    XVSize sized(mode_descr[modes.modes[mode_index]-
    			DC1394_VIDEO_MODE_160x120_YUV444].width,
                mode_descr[modes.modes[mode_index]-
			DC1394_VIDEO_MODE_160x120_YUV444].height);
    init_map(sized,4);
    if(optical_flag)
    {
       if (verbose) cerr << "Bayer filter selection only in Format7" << endl;
       throw(1);
    }

    if(verbose && 
        mode_descr[modes.modes[mode_index]-
	   DC1394_VIDEO_MODE_160x120_YUV444].mode==RGB8)
      cout << "Running RGB mode" <<endl;
    else
      cout << "Running non-RGB mode"<<endl;
  }else{ // format7
   if(dc1394_video_set_mode(camera_node,DC1394_VIDEO_MODE_FORMAT7_0)!=DC1394_SUCCESS)
   {
     if(verbose) cerr << "Couldn't switch Format 7" << endl;
     throw(8);
   }
   if( dc1394_format7_set_color_coding(camera_node,
             DC1394_VIDEO_MODE_FORMAT7_0, DC1394_COLOR_CODING_MONO8)!=
	     		DC1394_SUCCESS)
   {
     if(verbose) cerr << "Couldn't switch Format 7 Bayer" << endl;
     throw(9);
   }

   {
     unsigned int hmax, vmax;
     dc1394_format7_get_max_image_size(camera_node,DC1394_VIDEO_MODE_FORMAT7_0,
     		&hmax,&vmax);

     		
      float fbytesPerPacket= 7.5*0.000125*hmax*vmax*1;
      int ceilbytesPerPacket= static_cast<int>(ceil(fbytesPerPacket));
      int bytesPerPacket= (ceilbytesPerPacket+3)/4*4;

      dc1394_format7_set_packet_size(camera_node,DC1394_VIDEO_MODE_FORMAT7_0,
   		bytesPerPacket);
      XVSize sized(hmax,vmax);
      init_map(sized,4);
      dc1394_format7_set_roi(camera_node,DC1394_VIDEO_MODE_FORMAT7_0,
   		DC1394_COLOR_CODING_MONO8,DC1394_QUERY_FROM_CAMERA,
		0,0,hmax,vmax);
   }
   //if(optical_flag)
    //if(dc1394_format7_get_color_filter(camera_node,DC1394_VIDEO_MODE_FORMAT7_0,
    //					&optical_filter) !=DC1394_SUCCESS)
    //{
     // if (verbose) cerr << "Unable to set optical Bayer filter selected " << 
      //       optical_filter<< endl;
      //if (verbose) cerr<< "switching function off." << endl;
      //optical_flag=false;
    //}
    if(verbose) cout << "Running Format 7" <<endl;
  }
  // setting the gain 
  if( gain != -1 ) {
    unsigned int min_gain_val, max_gain_val;
    dc1394_feature_get_boundaries(camera_node,DC1394_FEATURE_GAIN,
    			&min_gain_val,&max_gain_val);
    if(gain <= max_gain_val && gain >= min_gain_val) {
      set_gain(gain);
    }else {
      if (verbose) cerr<< " the gain value is beyond limits[" << min_gain_val
          << ","<< max_gain_val<<"]..exiting"<<endl;
      throw(1);
    }
  }
  
  // set the saturation
  if( saturation != -1 ) {
    unsigned int min_sat_val, max_sat_val;
    dc1394_feature_get_boundaries(camera_node,DC1394_FEATURE_SATURATION,
    			&min_sat_val,&max_sat_val);
    if(saturation <= max_sat_val && saturation >= min_sat_val) {
      set_saturation(saturation);
    }else {
      if (verbose) cerr<< " the saturation value is beyond limits..exiting"<<endl;
      throw(1);
    }
  }

  // setting the sharpness
  if( sharpness != -1 ) {
    unsigned int min_sharp_val, max_sharp_val;
    dc1394_feature_get_boundaries(camera_node,DC1394_FEATURE_SHARPNESS,
    	    &min_sharp_val,&max_sharp_val);
    if(sharpness <= max_sharp_val && sharpness >= min_sharp_val) {
      set_sharpness(sharpness);
    }else {
      if (verbose) cerr<< " the sharpness value is beyond limits..exiting"<<endl;
      throw(1);
    }
  }

  // setting the shutter
  if( shutter != -1 ) {
    unsigned int min_shutter_val, max_shutter_val;
    dc1394_feature_get_boundaries(camera_node,DC1394_FEATURE_SHUTTER,
    			&min_shutter_val,&max_shutter_val);
    if(shutter <= max_shutter_val && shutter >= min_shutter_val) {
      set_shutter(shutter);
    }else {
      if (verbose) cerr<< " the shutter value is beyond limits [" << min_shutter_val
          << "," << max_shutter_val << "]..exiting"<<endl;
      throw(1);
    }
  }

  // setting the gamma
  if( gamma != -1 ) {
    unsigned int min_gamma_val, max_gamma_val;
    dc1394_feature_get_boundaries(camera_node,DC1394_FEATURE_GAMMA,
    				&min_gamma_val,&max_gamma_val);
    if(gamma <= max_gamma_val && gamma >= min_gamma_val) {
      set_gamma(gamma);
    }else {
      if (verbose) cerr<< " the gamma value "<< gamma <<" is beyond limits..exiting"<<endl;
      throw(1);
    }
  }

  // setting the exposure
  if( exposure != -1 ) {
    unsigned int min_exposure_val, max_exposure_val;
    dc1394_feature_get_boundaries(camera_node,DC1394_FEATURE_EXPOSURE,
                 &min_exposure_val,&max_exposure_val);
    if(exposure <= max_exposure_val && exposure >= min_exposure_val) {
      set_exposure(exposure);
    }else {
      if (verbose) cerr<< " the exposure value is beyond limits..exiting"<<endl;
      throw(1);
    }
  }

  // setting u and v components
  if(uv[0] >-1 || uv[1] > -1)
  {
    if(uv[0]*uv[1]<0) 
    {
      if (verbose) cerr << "just one of the white balance components set!!" << endl;
      throw(1);
    }
    dc1394_feature_whitebalance_set_value(camera_node,uv[0],uv[1]);
  }

  buffer_index=0;
  nowait_flag=false;

  if( ext_trigger ) {
    // setting the external trigger
    dc1394_software_trigger_set_power(camera_node,
			     ext_trigger ? DC1394_OFF : DC1394_ON );
    dc1394_external_trigger_set_mode( camera_node, DC1394_TRIGGER_MODE_1 );
  }
  //dc1394_feature_set_mode(camera_node,DC1394_FEATURE_BRIGHTNESS,DC1394_FEATURE_MODE_AUTO);
 // dc1394_feature_set_mode(camera_node,DC1394_FEATURE_EXPOSURE,DC1394_FEATURE_MODE_AUTO);
  if(verbose)
      cout << "Setting image size to " << dec <<frame(0).Width() << "x" <<
               frame(0).Height() << endl;
  init_tab();
  dc1394_capture_setup(camera_node,n_buffers,
              DC1394_CAPTURE_FLAGS_DEFAULT|DC1394_CAPTURE_FLAGS_AUTO_ISO);
  open(NULL);
  pthread_mutexattr_t tattr;
  pthread_mutexattr_init( &tattr );
  for(int i=0;i<n_buffers;i++)
     pthread_mutex_init( &wait_grab[i], &tattr );
  pthread_mutexattr_destroy( &tattr );
  pthread_attr_t attr ;
  pthread_attr_init( &attr );
  pthread_create(&grabber_thread,&attr,grab_thread,this);
  threadded=true;
  pthread_attr_destroy( &attr );
}

template <class IMTYPE>
IMTYPE & XVDig1394<IMTYPE>::current_frame_continuous(void)
{
   struct video1394_wait w_descr;

   if (!nowait_flag) {// Start the cycle
        nowait_flag = true;
        initiate_acquire(buffer_index);
        initiate_acquire(buffer_index+1);
	wait_for_completion(buffer_index);
   }

   return frame(buffer_index);
}
template class XVDig1394<XVImageRGB<XV_RGB15> >;
template class XVDig1394<XVImageRGB<XV_RGB16> >;
template class XVDig1394<XVImageRGB<XV_RGB24> >;
template class XVDig1394<XVImageRGB<XV_TRGB24> >;
template class XVDig1394<XVImageRGB<XV_RGBA32> >;
template class XVDig1394<XVImageRGB<XV_GLRGBA32> >;
template class XVDig1394<XVImageYUV<XV_YUV24> >;

#endif
