// *** BEGIN_XVISION2_COPYRIGHT_NOTICE ***
// *** END_XVISION2_COPYRIGHT_NOTICE ***

#ifndef _XVIMAGERGB_H_
#define _XVIMAGERGB_H_

#include <sys/types.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <XVMacros.h>
#include <XVList.h>
#include <XVColorImage.h>

using namespace std ;

/** class for RGB images */
template <class T>
class XVImageRGB : public XVColorBase<T> {

public:

   XVImageRGB(int width, int height, XVPixmap<T> * pixmap = NULL) : 
     XVColorBase<T>(width, height, pixmap) {}
   XVImageRGB(XVSize s, XVPixmap<T> * p ) :
     XVColorBase<T>(s.Width(), s.Height(), p) {}; 
   
   XVImageRGB(XVPixmap<T> * pixmap) : XVColorBase<T>(pixmap) {}
   XVImageRGB() : XVColorBase<T>() {}

  void CopyFrom(const XVImageRGB<T> &src_img)
       {
	 const u_char *src_ptr=(const u_char*)src_img.data();
         T *dst_ptr=this->lock();
	 assert(dst_ptr);
         for(int i=0;i<src_img.Height();i++)
          memcpy(dst_ptr+i*this->SizeX(),src_ptr+i*src_img.SizeX(),
	                                   this->SizeX());
	 this->unlock();
       };


	 /* ~XVImageRGB();*/

  // Type conversions etc. need to be outside the scope to allow
  // for necessary templating
	   
};

typedef enum {XV_REDBAND=0, XV_BLUEBAND=1,   XV_GREENBAND=2, XV_RGB_INTENSITY = 3} RGB_Projection;

#include <XVImageRGB.icc>

#endif
