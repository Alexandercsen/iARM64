#include <unistd.h>
#include <limits.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

static int WIDTH  = 1100;
static int HEIGHT = 740;
#define TOOLBAR_H  52
#define GUTTER_W   58
#define OUTPUT_H   160
#define MAX_LINES  4096
#define MAX_COLS   512
#define OUT_LINES  200
#define TAB_SIZE   4

static char lines[MAX_LINES][MAX_COLS];
static int  lineCount = 1;

static char outBuf[OUT_LINES][256];
static int  outCount = 0;

static int curX = 0, curY = 0;
static int scrY = 0, scrX = 0;

static char currentFile[512] = "program.s";
static int  isDirty = 0;

static int  mouseX = 0, mouseY = 0, mouseDown = 0;
static Uint32 lastBlink = 0;
static int  showCursor = 1;

static int  modalOpen   = 0;
static char modalInput[256] = "";
static int  modalInputLen = 0;

static SDL_Color cBg        = {16,16,20,255};
static SDL_Color cGutter    = {22,22,28,255};
static SDL_Color cToolbar   = {28,28,36,255};
static SDL_Color cLineHL    = {32,34,44,255};

static SDL_Color cText      = {218,218,218,255};
static SDL_Color cKeyword   = {252, 95,163,255};
static SDL_Color cRegister  = {100,180,255,255};
static SDL_Color cNumber    = {208,145,255,255};
static SDL_Color cComment   = {100,115,130,255};
static SDL_Color cString    = {250,200, 90,255};
static SDL_Color cLabel     = {120,210,180,255};
static SDL_Color cGray      = {110,115,130,255};
static SDL_Color cAccent    = { 10,132,255,255};
static SDL_Color cWhite     = {255,255,255,255};
static SDL_Color cRed       = {255,100,100,255};

static TTF_Font *gFont = NULL;
static int charH = 18;
static int charW = 9;

static const char *kw_instr[] = {
    "mov","movz","movk","movn","add","sub","mul","div","sdiv","udiv",
    "and","orr","eor","lsl","lsr","asr","ror",
    "ldr","ldrb","ldrh","ldrsb","ldrsh","ldrsw",
    "str","strb","strh",
    "bl","blr","br","b","bx","bne","beq","blt","bgt","ble","bge",
    "cmp","cmn","tst","teq",
    "ret","nop","svc","hlt","brk",
    "push","pop","adrp","adr","cbz","cbnz","tbz","tbnz",
    "madd","msub","smull","umull","umulh","smulh",
    "fmov","fadd","fsub","fmul","fdiv","fcmp","fcvt",
    NULL
};
static const char *kw_dir[] = {
    ".text",".data",".bss",".global",".globl",".extern",
    ".section",".align",".balign",".byte",".hword",".word",
    ".dword",".quad",".ascii",".asciz",".string",".space",
    ".skip",".fill",".set",".equ",".macro",".endm",
    ".include",".if",".ifdef",".ifndef",".endif",".else",
    NULL
};
static int isInstr(const char *w){for(int i=0;kw_instr[i];i++)if(!strcmp(w,kw_instr[i]))return 1;return 0;}
static int isDir  (const char *w){for(int i=0;kw_dir[i];i++)  if(!strcmp(w,kw_dir[i]))  return 1;return 0;}
static int isReg  (const char *w){
    if(!strcmp(w,"sp")||!strcmp(w,"lr")||!strcmp(w,"fp")||
       !strcmp(w,"pc")||!strcmp(w,"xzr")||!strcmp(w,"wzr")) return 1;
    if((w[0]=='x'||w[0]=='w')&&isdigit((unsigned char)w[1])) return 1;
    return 0;
}

static void setColor(SDL_Renderer *r, SDL_Color c){
    SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
}
static void fillRect(SDL_Renderer *r,int x,int y,int w,int h,SDL_Color c){
    setColor(r,c);
    SDL_Rect rc={x,y,w,h};
    SDL_RenderFillRect(r,&rc);
}
static void drawRect(SDL_Renderer *r,int x,int y,int w,int h,SDL_Color c){
    setColor(r,c);
    SDL_Rect rc={x,y,w,h};
    SDL_RenderDrawRect(r,&rc);
}

static void fillCircle(SDL_Renderer *r, int cx, int cy, int rad, SDL_Color c){
    setColor(r, c);
    for(int dy = -rad; dy <= rad; dy++){
        int dx = (int)sqrtf((float)(rad*rad - dy*dy));
        SDL_RenderDrawLine(r, cx-dx, cy+dy, cx+dx, cy+dy);
    }
}

static void fillPill(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c){
    int rad = h / 2;
    SDL_Rect body = { x + rad, y, w - rad*2, h };
    setColor(r, c);
    SDL_RenderFillRect(r, &body);
    fillCircle(r, x + rad,     y + rad, rad, c);
    fillCircle(r, x + w - rad, y + rad, rad, c);
}

static void drawText(SDL_Renderer *r,TTF_Font *f,
                     const char *t,int x,int y,SDL_Color c){
    if(!t||!*t)return;
    SDL_Surface *s=TTF_RenderText_Blended(f,t,c);
    if(!s)return;
    SDL_Texture *tx=SDL_CreateTextureFromSurface(r,s);
    SDL_Rect d={x,y,s->w,s->h};
    SDL_RenderCopy(r,tx,NULL,&d);
    SDL_FreeSurface(s);
    SDL_DestroyTexture(tx);
}
static int tw(const char *t){int w=0;TTF_SizeText(gFont,t,&w,NULL);return w;}

static void drawButton(SDL_Renderer *r, const char *label,
                       int x, int y, int w, int h, int accent){
    int hov = mouseX>=x && mouseX<=x+w && mouseY>=y && mouseY<=y+h;
    int prs = hov && mouseDown;

    SDL_Color fill, tc;
    if(prs){
        fill = (SDL_Color){230,230,230,255};
        tc   = (SDL_Color){20, 20, 30, 255};
    } else if(accent){
        fill = hov ? (SDL_Color){50,160,255,255} : cAccent;
        tc   = cWhite;
    } else {
        fill = hov ? (SDL_Color){72,76,92,255} : (SDL_Color){50,54,68,255};
        tc   = cWhite;
    }

    fillPill(r, x, y, w, h, fill);

    int lw = tw(label);
    drawText(r, gFont, label, x+(w-lw)/2, y+(h-charH)/2, tc);
}

static void drawToolbar(SDL_Renderer *r){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_Color tb={28,28,36,240};
    fillRect(r,0,0,WIDTH,TOOLBAR_H,tb);

    SDL_Color sep={55,55,75,200};
    SDL_SetRenderDrawColor(r,sep.r,sep.g,sep.b,sep.a);
    SDL_RenderDrawLine(r,0,TOOLBAR_H-1,WIDTH,TOOLBAR_H-1);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);

    int bh=28, by=(TOOLBAR_H-bh)/2;
    drawButton(r,"Run",  10, by, 88, bh, 1);
    drawButton(r,"New",  108, by, 78, bh, 0);
    drawButton(r,"Save",196, by, 88, bh, 0);


    char title[600];
    snprintf(title,sizeof(title),"%s%s",currentFile,isDirty?" *":"");
    int ftw=tw(title);
    drawText(r,gFont,title,WIDTH-ftw-14,(TOOLBAR_H-charH)/2,cGray);
}

static void drawCodeLine(SDL_Renderer *r,char *line,int x,int y){
    int i=0;
    while(line[i]){
        if(line[i]==';'||line[i]=='/'){
            drawText(r,gFont,&line[i],x,y,cComment); break;
        }
        if(line[i]=='.'){
            char word[64]; int j=0;
            word[j++]=line[i++];
            while(isalnum((unsigned char)line[i])||line[i]=='_')
                word[j++]=line[i++];
            word[j]=0;
            drawText(r,gFont,word,x,y,isDir(word)?cKeyword:cText);
            x+=tw(word); continue;
        }
        if(line[i]=='"'){
            char word[MAX_COLS]; int j=0;
            word[j++]=line[i++];
            while(line[i]&&!(line[i]=='"'&&word[j-1]!='\\'))
                word[j++]=line[i++];
            if(line[i])word[j++]=line[i++];
            word[j]=0;
            drawText(r,gFont,word,x,y,cString);
            x+=tw(word); continue;
        }
        if(line[i]=='#'){
            char word[64]; int j=0;
            word[j++]=line[i++];
            while(isalnum((unsigned char)line[i])||line[i]=='x'||line[i]=='-')
                word[j++]=line[i++];
            word[j]=0;
            drawText(r,gFont,word,x,y,cNumber);
            x+=tw(word); continue;
        }
        if(isdigit((unsigned char)line[i])){
            char word[64]; int j=0;
            while(isxdigit((unsigned char)line[i])||line[i]=='x'||line[i]=='X')
                word[j++]=line[i++];
            word[j]=0;
            drawText(r,gFont,word,x,y,cNumber);
            x+=tw(word); continue;
        }
        if(isalpha((unsigned char)line[i])||line[i]=='_'){
            char word[64]; int j=0;
            while(isalnum((unsigned char)line[i])||line[i]=='_'||line[i]=='.')
                word[j++]=line[i++];
            word[j]=0;
            SDL_Color col=cText;
            if(isInstr(word))      col=cKeyword;
            else if(isReg(word))   col=cRegister;
            else if(line[i]==':')  col=cLabel;
            drawText(r,gFont,word,x,y,col);
            x+=tw(word); continue;
        }
        char c[2]={line[i++],0};
        drawText(r,gFont,c,x,y,cText);
        x+=tw(c);
    }
}

static void drawOutput(SDL_Renderer *r){
    int oy=HEIGHT-OUTPUT_H;
    fillRect(r,0,oy,WIDTH,OUTPUT_H,(SDL_Color){14,14,18,255});
    SDL_SetRenderDrawColor(r,50,50,70,255);
    SDL_RenderDrawLine(r,0,oy,WIDTH,oy);
    drawText(r,gFont,"Console",12,oy+6,cGray);
    int maxL=(OUTPUT_H-28)/charH;
    for(int i=0;i<outCount&&i<maxL;i++){
        SDL_Color col=(strstr(outBuf[i],"error")||strstr(outBuf[i],"Error"))
            ?cRed:cText;
        drawText(r,gFont,outBuf[i],12,oy+26+i*charH,col);
    }
    if(outCount==0) drawText(r,gFont,"No output.",12,oy+26,cGray);
}

static void drawEditorCursor(SDL_Renderer *r){
    int vis=curY-scrY;
    int edH=HEIGHT-TOOLBAR_H-OUTPUT_H;
    if(vis<0||vis>=edH/charH)return;
    int y=TOOLBAR_H+6+vis*charH;
    char tmp[MAX_COLS]; strncpy(tmp,lines[curY],curX); tmp[curX]=0;
    int x=GUTTER_W+8+tw(tmp)-scrX;
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,cAccent.r,cAccent.g,cAccent.b,220);
    SDL_Rect cr={x,y,2,charH-2};
    SDL_RenderFillRect(r,&cr);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
}

static void drawModal(SDL_Renderer *r){
    if(!modalOpen)return;


    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,0,0,0,160);
    SDL_Rect full={0,0,WIDTH,HEIGHT};
    SDL_RenderFillRect(r,&full);


    int cw=420,ch=170;
    int cx=(WIDTH-cw)/2, cy=(HEIGHT-ch)/2;
    fillRect(r,cx,cy,cw,ch,(SDL_Color){36,38,50,255});


    SDL_SetRenderDrawColor(r,70,75,100,255);

    SDL_RenderDrawLine(r,cx+14,cy,     cx+cw-14,cy);
    SDL_RenderDrawLine(r,cx+14,cy+ch-1,cx+cw-14,cy+ch-1);
    SDL_RenderDrawLine(r,cx,   cy+14,  cx,       cy+ch-14);
    SDL_RenderDrawLine(r,cx+cw-1,cy+14,cx+cw-1, cy+ch-14);

    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);


    drawText(r,gFont,"New File",cx+20,cy+18,cWhite);


    drawText(r,gFont,"Enter a filename (e.g. hello.s)",cx+20,cy+44,cGray);


    int bx=cx+20, by2=cy+72, bw=cw-40, bh2=34;
    fillRect(r,bx,by2,bw,bh2,(SDL_Color){20,22,30,255});
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,cAccent.r,cAccent.g,cAccent.b,200);
    SDL_RenderDrawRect(r,&(SDL_Rect){bx,by2,bw,bh2});
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);


    char display[300];
    snprintf(display,sizeof(display),"%s",modalInput);
    drawText(r,gFont,display,bx+10,by2+(bh2-charH)/2,cText);
    if(showCursor){
        int cx2=bx+10+tw(display);
        SDL_SetRenderDrawColor(r,cAccent.r,cAccent.g,cAccent.b,255);
        SDL_RenderDrawLine(r,cx2,by2+5,cx2,by2+bh2-6);
    }


    drawButton(r,"Cancel", cx+cw-210, cy+ch-46, 88, 30, 0);
    drawButton(r,"Create", cx+cw-112, cy+ch-46, 92, 30, 1);
}

static void insertChar(char c){
    int len=(int)strlen(lines[curY]);
    if(len<MAX_COLS-1){
        memmove(&lines[curY][curX+1],&lines[curY][curX],len-curX+1);
        lines[curY][curX++]=c; isDirty=1;
    }
}
static void insertTab(){
    int spaces=TAB_SIZE-(curX%TAB_SIZE);
    for(int i=0;i<spaces;i++)insertChar(' ');
}
static void newLineEditor(){
    if(lineCount>=MAX_LINES-1)return;
    for(int i=lineCount;i>curY+1;i--) strcpy(lines[i],lines[i-1]);
    strcpy(lines[curY+1],&lines[curY][curX]);
    lines[curY][curX]=0;
    int indent=0;
    while(lines[curY][indent]==' ')indent++;
    char ind[MAX_COLS]; memset(ind,' ',indent); ind[indent]=0;
    char rest[MAX_COLS]; strcpy(rest,lines[curY+1]);
    strcpy(lines[curY+1],ind); strcat(lines[curY+1],rest);
    curY++; curX=indent; lineCount++; isDirty=1;
}
static void backspace(){
    if(curX>0){
        memmove(&lines[curY][curX-1],&lines[curY][curX],
                strlen(lines[curY])-curX+1);
        curX--;
    } else if(curY>0){
        curX=(int)strlen(lines[curY-1]);
        strcat(lines[curY-1],lines[curY]);
        for(int i=curY;i<lineCount-1;i++) strcpy(lines[i],lines[i+1]);
        curY--; lineCount--;
    }
    isDirty=1;
}
static void copyLine() { SDL_SetClipboardText(lines[curY]); }
static void pasteText(){
    char *clip=SDL_GetClipboardText(); if(!clip)return;
    for(int i=0;clip[i];i++){
        if(clip[i]=='\n') newLineEditor(); else insertChar(clip[i]);
    }
    SDL_free(clip);
}
static void cutLine(){
    SDL_SetClipboardText(lines[curY]);
    for(int i=curY;i<lineCount-1;i++) strcpy(lines[i],lines[i+1]);
    lines[lineCount-1][0]=0; lineCount--;
    if(curY>=lineCount) curY=lineCount-1;
    curX=0; isDirty=1;
}

static void loadFile(const char *path){
    FILE *f=fopen(path,"r"); if(!f)return;
    lineCount=0; memset(lines,0,sizeof(lines));
    char buf[MAX_COLS];
    while(fgets(buf,sizeof(buf),f)&&lineCount<MAX_LINES){
        int len=(int)strlen(buf);
        if(len>0&&buf[len-1]=='\n') buf[len-1]=0;
        strncpy(lines[lineCount++],buf,MAX_COLS-1);
    }
    fclose(f);
    if(lineCount==0) lineCount=1;
    curX=curY=scrY=scrX=0; isDirty=0;
    strncpy(currentFile,path,sizeof(currentFile)-1);
    outCount=0;
}
static void saveFile(const char *path){
    FILE *f=fopen(path,"w"); if(!f)return;
    for(int i=0;i<lineCount;i++) fprintf(f,"%s\n",lines[i]);
    fclose(f); isDirty=0;
}
static void clearEditor(){
    memset(lines,0,sizeof(lines)); lineCount=1;
    curX=curY=scrY=scrX=0; isDirty=0; outCount=0;
}

static void runCode(){
    outCount=0;
    saveFile(currentFile);
    int ok=system("clang program.s -o program 2>err.txt");
    FILE *f=fopen("err.txt","r");
    if(f){
        char buf[256];
        while(fgets(buf,256,f)&&outCount<OUT_LINES){
            buf[strcspn(buf,"\n")]=0;
            strncpy(outBuf[outCount++],buf,255);
        }
        fclose(f);
    }
    if(ok!=0)return;
    char cwd[PATH_MAX]; getcwd(cwd,sizeof(cwd));
    char cmd[2048];
    snprintf(cmd,sizeof(cmd),
        "osascript -e 'tell application \"Terminal\" to do script "
        "\"cd %s; ./program; echo; echo ---- done ----;\"'",cwd);
    system(cmd);
}

static void handleClick(int x,int y){

    if(modalOpen){
        int cw=420,ch=170;
        int mcx=(WIDTH-cw)/2, mcy=(HEIGHT-ch)/2;

        int canx=mcx+cw-210, cany=mcy+ch-46;
        if(x>=canx&&x<=canx+88&&y>=cany&&y<=cany+30){
            modalOpen=0; memset(modalInput,0,sizeof(modalInput));
            modalInputLen=0; SDL_StopTextInput(); SDL_StartTextInput();
        }

        int crex=mcx+cw-112, crey=mcy+ch-46;
        if(x>=crex&&x<=crex+92&&y>=crey&&y<=crey+30){
            if(modalInputLen>0){
                clearEditor();
                strncpy(currentFile,modalInput,sizeof(currentFile)-1);
                int flen=(int)strlen(currentFile);
                if(flen<2||strcmp(currentFile+flen-2,".s")!=0){
                    if(flen<510) strcat(currentFile,".s");
                }
                saveFile(currentFile);
            }
            modalOpen=0; memset(modalInput,0,sizeof(modalInput));
            modalInputLen=0;
        }
        return;
    }

    int bh=28,by=(TOOLBAR_H-bh)/2;

    if(x>=10&&x<=98&&y>=by&&y<=by+bh){ saveFile(currentFile); runCode(); return; }

    if(x>=108&&x<=186&&y>=by&&y<=by+bh){
        memset(modalInput,0,sizeof(modalInput));
        modalInputLen=0;
        modalOpen=1;
        return;
    }

    if(x>=196&&x<=284&&y>=by&&y<=by+bh){ saveFile(currentFile); return; }


    if(y>TOOLBAR_H&&y<HEIGHT-OUTPUT_H){
        int line=(y-(TOOLBAR_H+6))/charH+scrY;
        if(line<0)line=0;
        if(line>=lineCount)line=lineCount-1;
        curY=line;
        int px=x-(GUTTER_W+8)+scrX;
        int len=(int)strlen(lines[curY]);
        curX=0; int accum=0;
        for(int i=0;i<len;i++){
            char tmp[2]={lines[curY][i],0};
            int cw2=tw(tmp);
            if(accum+cw2/2>=px)break;
            accum+=cw2; curX++;
        }
    }
}

static void clampScroll(){
    int edH=HEIGHT-TOOLBAR_H-OUTPUT_H;
    int vis=edH/charH;
    if(curY<scrY) scrY=curY;
    if(curY>=scrY+vis) scrY=curY-vis+1;
    if(scrY<0) scrY=0;
    if(scrY>lineCount-1) scrY=lineCount-1;
}

int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();


    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    SDL_Window *win=SDL_CreateWindow(
        "iARM 64  -  Assembly Editor",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        WIDTH,HEIGHT,
        SDL_WINDOW_RESIZABLE);

    SDL_Renderer *r=SDL_CreateRenderer(win,-1,
        SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);

    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);


    TTF_Font *font=NULL;
    const char *fps[]={
        "JetBrainsMono-Regular.ttf",
        "/Library/Fonts/JetBrainsMono-Regular.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        NULL
    };
    for(int i=0;fps[i]&&!font;i++) font=TTF_OpenFont(fps[i],15);
    if(!font){SDL_Log("No font.");return 1;}
    gFont=font;


    TTF_SizeText(font,"A",&charW,&charH);
    charH+=2;

    SDL_StartTextInput();

    int running=1;
    SDL_Event ev;

    while(running){

        if(SDL_GetTicks()-lastBlink>530){
            showCursor=!showCursor; lastBlink=SDL_GetTicks();
        }

        while(SDL_PollEvent(&ev)){
            switch(ev.type){
            case SDL_QUIT: running=0; break;

            case SDL_DROPFILE:{
                char *dropped = ev.drop.file;
                if(!modalOpen && dropped && dropped[0]){
                    char pathcopy[512];
                    strncpy(pathcopy, dropped, 511);
                    pathcopy[511] = 0;
                    SDL_free(dropped);
                    ev.drop.file = NULL;
                    loadFile(pathcopy);
                }
                break;
            }

            case SDL_WINDOWEVENT:
                if(ev.window.event==SDL_WINDOWEVENT_RESIZED||
                   ev.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
                    SDL_GetWindowSize(win,&WIDTH,&HEIGHT);
                }
                break;

            case SDL_TEXTINPUT:
                if(modalOpen){

                    for(int i=0;ev.text.text[i]&&modalInputLen<255;i++){
                        modalInput[modalInputLen++]=ev.text.text[i];
                        modalInput[modalInputLen]=0;
                    }
                } else {
                    insertChar(ev.text.text[0]);
                    showCursor=1; lastBlink=SDL_GetTicks();
                }
                break;

            case SDL_MOUSEMOTION:
                mouseX=ev.motion.x; mouseY=ev.motion.y; break;
            case SDL_MOUSEBUTTONDOWN:
                mouseDown=1; handleClick(ev.button.x,ev.button.y); break;
            case SDL_MOUSEBUTTONUP:
                mouseDown=0; break;

            case SDL_MOUSEWHEEL:
                if(!modalOpen){
                    scrY-=ev.wheel.y*3;
                    if(scrY<0)scrY=0;
                    if(scrY>lineCount-1)scrY=lineCount-1;
                }
                break;

            case SDL_KEYDOWN:{
                SDL_Keycode key=ev.key.keysym.sym;
                SDL_Keymod  mod=SDL_GetModState();
                int isCmd =(mod&KMOD_GUI)!=0;
                int isCtrl=(mod&KMOD_CTRL)!=0;
                showCursor=1; lastBlink=SDL_GetTicks();

                if(modalOpen){

                    if(key==SDLK_ESCAPE){
                        modalOpen=0;
                        memset(modalInput,0,sizeof(modalInput));
                        modalInputLen=0;
                    } else if(key==SDLK_RETURN&&modalInputLen>0){
                        clearEditor();
                        strncpy(currentFile,modalInput,sizeof(currentFile)-1);
                        int flen2=(int)strlen(currentFile);
                        if(flen2<2||strcmp(currentFile+flen2-2,".s")!=0){
                            if(flen2<510) strcat(currentFile,".s");
                        }
                        saveFile(currentFile);
                        modalOpen=0;
                        memset(modalInput,0,sizeof(modalInput));
                        modalInputLen=0;
                    } else if(key==SDLK_BACKSPACE&&modalInputLen>0){
                        modalInput[--modalInputLen]=0;
                    }
                    break;
                }


                if(isCmd||isCtrl){
                    switch(key){
                    case SDLK_c: copyLine();  break;
                    case SDLK_v: pasteText(); break;
                    case SDLK_x: cutLine();   break;
                    case SDLK_s: saveFile(currentFile); break;
                    case SDLK_n:
                        memset(modalInput,0,sizeof(modalInput));
                        modalInputLen=0; modalOpen=1; break;
                    }
                } else {
                    int len=(int)strlen(lines[curY]);
                    switch(key){
                    case SDLK_BACKSPACE: backspace(); break;
                    case SDLK_RETURN:    newLineEditor(); break;
                    case SDLK_TAB:       insertTab(); break;
                    case SDLK_LEFT:
                        if(curX>0)curX--;
                        else if(curY>0){curY--;curX=(int)strlen(lines[curY]);}
                        break;
                    case SDLK_RIGHT:
                        if(curX<len)curX++;
                        else if(curY<lineCount-1){curY++;curX=0;}
                        break;
                    case SDLK_UP:
                        if(curY>0){curY--;int l=(int)strlen(lines[curY]);if(curX>l)curX=l;}
                        break;
                    case SDLK_DOWN:
                        if(curY<lineCount-1){curY++;int l=(int)strlen(lines[curY]);if(curX>l)curX=l;}
                        break;
                    case SDLK_HOME: curX=0; break;
                    case SDLK_END:  curX=(int)strlen(lines[curY]); break;
                    case SDLK_PAGEUP:
                        curY-=20; if(curY<0)curY=0; break;
                    case SDLK_PAGEDOWN:
                        curY+=20; if(curY>=lineCount)curY=lineCount-1; break;
                    case SDLK_DELETE:
                        if(curX<len){
                            memmove(&lines[curY][curX],&lines[curY][curX+1],len-curX);
                            isDirty=1;
                        }
                        break;
                    }
                }
                break;
            }
            }
        }

        if(!modalOpen) clampScroll();


        fillRect(r,0,0,WIDTH,HEIGHT,cBg);


        fillRect(r,0,TOOLBAR_H,GUTTER_W,
                 HEIGHT-TOOLBAR_H-OUTPUT_H,cGutter);

        int edH=HEIGHT-TOOLBAR_H-OUTPUT_H;
        int vis=edH/charH+1;
        int yOff=TOOLBAR_H+6;

        for(int i=scrY;i<lineCount&&i<scrY+vis;i++){
            int ly=yOff+(i-scrY)*charH;
            if(i==curY)
                fillRect(r,GUTTER_W,ly,WIDTH-GUTTER_W,charH,cLineHL);

            char ln[16]; snprintf(ln,sizeof(ln),"%d",i+1);
            int lnw=tw(ln);
            drawText(r,gFont,ln,GUTTER_W-lnw-6,ly,
                     i==curY?cAccent:cGray);

            drawCodeLine(r,lines[i],GUTTER_W+8-scrX,ly);
        }

        drawOutput(r);
        drawToolbar(r);
        if(showCursor&&!modalOpen) drawEditorCursor(r);
        drawModal(r);

        SDL_RenderPresent(r);
    }

    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}