#define super IOCommand

class SASMegaRAID;
class mraid_ccbCommand: public IOCommand {
    OSDeclareDefaultStructors(mraid_ccbCommand);
public:
    struct st {
        void            *ccb_context;
    
        /* Do things painless, avoid pointer to member function */
        void (*ccb_done)(mraid_ccbCommand *);
    
        UInt32          ccb_direction;
#define MRAID_DATA_NONE	0
#define MRAID_DATA_IN   1
#define MRAID_DATA_OUT	2
    
        mraid_frame     *ccb_frame;
        UInt32          ccb_frame_size;
        UInt32          ccb_extra_frames;
        
        addr64_t        ccb_pframe;
        //UInt32        ccb_pframe_offset;
        
        mraid_sense     *ccb_sense;
        addr64_t        ccb_psense;
    
        mraid_sgl       *ccb_sgl;
        mraid_sgl_mem   ccb_sglmem;
    } s;

    void scrubCommand() {
        s.ccb_frame->mrr_header.mrh_cmd_status = 0x0;
        s.ccb_frame->mrr_header.mrh_flags = 0x0;
        
        s.ccb_context = NULL;
        s.ccb_done = NULL;
        s.ccb_direction = 0;
        s.ccb_frame_size = 0;
        s.ccb_extra_frames = 0;
        s.ccb_sgl = NULL;
        
        memset(&s.ccb_sglmem, 0, sizeof(mraid_sgl_mem));
    }
protected:
    virtual bool init() {
        if(!super::init())
            return false;
        
        memset(&s, 0, sizeof(struct st));
        
        return true;
    }
    virtual void free() {
        FreeSGL(&s.ccb_sglmem);
        
        super::free();
    }
public:
    static mraid_ccbCommand *NewCommand() {
        mraid_ccbCommand *me = new mraid_ccbCommand;
        
        return me;
    }
};