#include "XVPv.h"



static
void Sleep(unsigned int time)
{
    struct timespec t,r;
    
    t.tv_sec    = time / 1000;
    t.tv_nsec   = (time % 1000) * 1000000;    
    
    while(nanosleep(&t,&r)==-1)
        t = r;
}

template <class IMTYPE>
const char* XVPv<IMTYPE>::TypeToString(tPvDatatype aType)
{
    switch(aType)
    {
        case ePvDatatypeUnknown:
            return "unknown";
        case ePvDatatypeCommand:
            return "command";
        case ePvDatatypeRaw:
            return "raw";
        case ePvDatatypeString:
            return "string";
        case ePvDatatypeEnum:
            return "enum";
        case ePvDatatypeUint32:
            return "uint32";
        case ePvDatatypeFloat32:
            return "float32";
        default:
            return "";
    }
}

template <class IMTYPE>
tPvErr XVPv<IMTYPE>::AttrList(tPvHandle Camera)
{
    tPvErr          Err;
    tPvUint32       Count;
    tPvAttrListPtr  Attrs;

    if(!(Err = PvAttrList(Camera,&Attrs,&Count)))
        for(tPvUint32 i=0;i<Count;i++)
            printf("%s\n",Attrs[i]);

    return Err;    
}

template <class IMTYPE>
tPvErr XVPv<IMTYPE>::AttrType(tPvHandle Camera,const char* Label)
{
    tPvErr              Err;
    tPvAttributeInfo    Info;

    if(!(Err = PvAttrInfo(Camera,Label,&Info)))
        printf("%s\n",TypeToString(Info.Datatype));    

    return Err;    
}

template <class IMTYPE>
tPvErr XVPv<IMTYPE>::AttrRange(tPvHandle Camera,const char* Label)
{
    tPvErr              Err;
    tPvAttributeInfo    Info;

    if(!(Err = PvAttrInfo(Camera,Label,&Info)))
        switch(Info.Datatype)
        {
            case ePvDatatypeEnum:
            {
                char Range[512];

                if(!(Err = PvAttrRangeEnum(Camera,Label,Range,512,NULL)))
                    printf("%s\n",Range);

                break;
            }
            case ePvDatatypeUint32:
            {
                tPvUint32 Min,Max;

                if(!(Err = PvAttrRangeUint32(Camera,Label,&Min,&Max)))
                    printf("%lu : %lu\n",Min,Max);

                break;
            }
            case ePvDatatypeFloat32:
            {
                tPvFloat32 Min,Max;

                if(!(Err = PvAttrRangeFloat32(Camera,Label,&Min,&Max)))
                    printf("%f : %f\n",Min,Max);

                break;
            }
            default:
                break;
        }

    return Err;
}

template <class IMTYPE>
tPvErr XVPv<IMTYPE>::AttrRead(tPvHandle Camera,const char* Label)
{
    tPvErr              Err;
    tPvAttributeInfo    Info;

    if(!(Err = PvAttrInfo(Camera,Label,&Info)))
        switch(Info.Datatype)
        {
            case ePvDatatypeString:
            {
                char String[256];

                if(!(Err = PvAttrStringGet(Camera,Label,String,256,NULL)))
                    printf("%s\n",String);

                break;
            }
            case ePvDatatypeEnum:
            {
                char String[256];

                if(!(Err = PvAttrEnumGet(Camera,Label,String,256,NULL)))
                    printf("%s\n",String);

                break;
            }
            case ePvDatatypeUint32:
            {
                tPvUint32 Value;

                if(!(Err = PvAttrUint32Get(Camera,Label,&Value)))
                    printf("%lu\n",Value);

                break;
            }
            case ePvDatatypeFloat32:
            {
                tPvFloat32 Value;

                if(!(Err = PvAttrFloat32Get(Camera,Label,&Value)))
                    printf("%f\n",Value);

                break;
            }
            default:
                break;
        }

    return Err;
}

template <class IMTYPE>
tPvErr XVPv<IMTYPE>::AttrWrite(tPvHandle Camera,const char* Label,const char* Value)
{
    tPvErr              Err;
    tPvAttributeInfo    Info;

    if(!(Err = PvAttrInfo(Camera,Label,&Info)))
        switch(Info.Datatype)
        {
            case ePvDatatypeEnum:
            {
                Err = PvAttrEnumSet(Camera,Label,Value);

                break;
            }
            case ePvDatatypeUint32:
            {
                tPvUint32 Uint;

                if(sscanf(Value,"%lu",&Uint) == 1)
                    Err = PvAttrUint32Set(Camera,Label,Uint);
                else
                    Err = ePvErrWrongType;

                break;
            }
            case ePvDatatypeFloat32:
            {
                tPvFloat32 Float;

                if(sscanf(Value,"%f",&Float) == 1)
                    Err = PvAttrFloat32Set(Camera,Label,Float);
                else
                    Err = ePvErrWrongType;

                break;
            }
            default:
                break;
        }

    return Err;
}

template <class IMTYPE>
IMTYPE &XVPv<IMTYPE>::current_frame_continuous()
{
  return frame(buffer_index);
}

template <class IMTYPE>
void XVPv<IMTYPE>::close()
{
}

template <class IMTYPE>
int XVPv<IMTYPE>::open(const char*name)
{
  return 1;
}


template <class IMTYPE>
int XVPv<IMTYPE>::wait_for_completion(int i_frame)
{
  if(i_frame>=0 && i_frame<n_buffers)
  {
    PvCaptureWaitForFrameDone(Handle,&(pv_buffers[i_frame]),PVINFINITE);
  }
  else
    throw 33;
  return 1;
}

template <class IMTYPE>
int XVPv<IMTYPE>::initiate_acquire(int i_frame)
{
  if(i_frame>=0 && i_frame<n_buffers)
  {
    PvCaptureQueueFrame(Handle,&(pv_buffers[i_frame]),NULL);
  }
  else
    throw 33;
  return 1;
}


template <class IMTYPE>
int XVPv<IMTYPE>::set_params(char *paramstring)
{
  XVParser parse_result;
  char     string[10];
  while(parse_param(paramstring,parse_result)>0)
  {
    switch(parse_result.c)
    {
    case 'r':
      sprintf(string,"%5.2f",parse_result.val/10.0);
      if(verbose) cerr << "framerate " << string << "[Hz]" << endl;
      if(AttrWrite(Handle,"FrameRate",string))
      {
         cerr << "invalid rate " << string << "[Hz] defaulting to 30Hz" 
	      << endl;
      }
      break;
    default:
      cerr << parse_result.c << "=" << parse_result.val
           << " is not supported by XVPv (skipping)" << endl;
      break;
    }
  }
  return 1;
}

template <class IMTYPE>
XVPv<IMTYPE>::XVPv(unsigned long u_id,char*param,bool in_verbose):
                                XVVideo<IMTYPE>("","")
{
   tPvCameraInfo camera_list[XVPV_MAX_CAMERAS];
   unsigned long          num_cameras,frame_size;
   bool			  found=false;
   static bool            initialized=false;
   int i,found_i;
   XVSize		  size(640,480);


   buffer_index=0;pv_buffers=NULL;verbose=in_verbose;
   if(verbose)
   {
      unsigned long major,minor;
      PvVersion(&major, &minor);
      cout << "Version " << major << "." << minor<<endl;
   }
   if(!initialized && PvInitialize()!=ePvErrSuccess)
   {
     cerr << "Could not initialize Pv-System" << endl;
     throw 11;
   }
   initialized=true;
   while(!PvCameraCount()) Sleep(250);
   num_cameras=PvCameraList(camera_list,XVPV_MAX_CAMERAS,NULL);
   if(verbose) cout << num_cameras << " cameras found" << endl;
   if(u_id)
    for(i=0;i< num_cameras && !found;i++)
    {
      if(u_id!=camera_list[i].UniqueId) continue;
      found=true; found_i=i;
    }
   else
   {
      if(num_cameras>0) i=0,found=true;
   }
   if(!found) {cerr << "Couldn't find camera"<< endl;throw 12;}
   if(verbose)
   {
        cout << "UniqueId " << camera_list[found_i].UniqueId << endl;
        cout << "SerialString " << camera_list[found_i].SerialString 
	     << endl;
        cout << "DisplayName " << camera_list[found_i].DisplayName << endl;
   }
   if(PvCameraOpen(camera_list[found_i].UniqueId,ePvAccessMaster,&Handle))
   {
      cerr << "Couldn't open camera" << endl;
      throw 13;
   }
   //if(verbose) AttrList(Handle);
   tPvUint32 nBytesMax;
   // get the last packet size set on the camera
   //if(PvAttrUint32Get( Handle, "PacketSize", &nBytesMax )) throw 19;
   //nBytesMax=1518;
   //if(PvCaptureAdjustPacketSize( Handle, nBytesMax ))        throw 19;
   if(AttrWrite(Handle,"PixelFormat","Bgra32")) throw 21;
   if(AttrWrite(Handle,"FrameRate","30")) throw 21;
   if(PvAttrUint32Set(Handle,"Width",size.Width())) throw 20;
   if(PvAttrUint32Set(Handle,"Height",size.Height())) throw 20;
   if(PvAttrUint32Set(Handle,"Height",size.Height())) throw 20;

   init_map(size,2);
   PvCaptureStart(Handle);
   if(AttrWrite(Handle,"FrameRate","30")) throw 21;
   set_params(param);
   if(PvAttrEnumSet(Handle,"FrameStartTriggerMode","FixedRate"))
   					  throw 21;
   PvCommandRun(Handle,"AcquisitionStart");;
   pv_buffers=new tPvFrame[n_buffers];
   PvAttrUint32Get(Handle,"TotalBytesPerFrame",&frame_size);
   for(i=0;i<n_buffers;i++)
   {
    pv_buffers[i].Context[0]=Handle;
    pv_buffers[i].ImageBuffer=frame(i).lock();
    frame(i).unlock();
    pv_buffers[i].ImageBufferSize=frame_size;
   }
}

template <class IMTYPE>
XVPv<IMTYPE>::~XVPv()
{
  PvCommandRun(Handle,"AcquisitionStop");
  PvCaptureEnd(Handle);
  PvCaptureQueueClear(Handle);
  PvCameraClose(Handle);
  PvUnInitialize();
  if(pv_buffers) delete [] pv_buffers;
}

template class XVPv<XVImageRGB<XV_RGBA32> >;

