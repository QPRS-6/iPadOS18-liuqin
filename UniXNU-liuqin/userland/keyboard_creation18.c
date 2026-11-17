/* SPDX-License-Identifier: MIT
 * Bounded native22H374 keyboard creation/handoff observations. No input text,
 * synthetic views, lifecycle answers, service waits or GPU state are changed.
 */
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define KEYBOARD_RECT "{CGRect={CGPoint=dd}{CGSize=dd}}"
#define KEYBOARD_SIZE "{CGSize=dd}"
#define KEYBOARD_CREATION_LIMIT 8
static struct {Class cls; SEL selector; IMP original; unsigned logged;} creation[14];
static bool creation_installed;
void pong_keyboard_creation18_install(void);
struct creation_message {char text[1536];size_t used;};
static void creation_append(struct creation_message *message,const char *format,...)
{
    size_t available=sizeof(message->text)-message->used;
    if(available<=1)return;
    va_list arguments;va_start(arguments,format);
    int count=vsnprintf(message->text+message->used,available,format,arguments);
    va_end(arguments);
    if(count>0)message->used+=(size_t)count<available?(size_t)count:available-1;
}
static void creation_emit(const struct creation_message *message)
{fprintf(stderr,"%s\n",message->text);}

static bool creation_type(const char *actual,const char *expected)
{
    if(!actual || !expected)return false;
    while(*actual && strchr("rnNoORV",*actual))actual++;
    if(*expected=='{')return !strcmp(actual,expected);
    return *actual && strchr(expected,*actual)!=NULL;
}
static bool creation_abi(Method method,const char *result,unsigned count,const char *const *arguments)
{
    if(!method || method_getNumberOfArguments(method)!=count+2)return false;
    char *type=method_copyReturnType(method);bool valid=creation_type(type,result);free(type);
    for(unsigned i=0;valid && i<count;i++) {
        type=method_copyArgumentType(method,i+2);valid=creation_type(type,arguments[i]);free(type);
    }
    return valid;
}
static unsigned creation_record(unsigned index)
{
    unsigned count=__atomic_load_n(&creation[index].logged,__ATOMIC_RELAXED);
    // Logging contention may lose a record, but must never delay native work.
    for(unsigned attempt=0;attempt<KEYBOARD_CREATION_LIMIT && count<KEYBOARD_CREATION_LIMIT;attempt++)
        if(__atomic_compare_exchange_n(&creation[index].logged,&count,count+1,true,
            __ATOMIC_RELAXED,__ATOMIC_RELAXED))return count+1;
    return 0;
}
static IMP creation_original(unsigned index)
{return __atomic_load_n(&creation[index].original,__ATOMIC_ACQUIRE);}
static const char *creation_class(id object)
{return object?class_getName(object_getClass(object)):"nil";}
static Ivar creation_ivar(id object,const char *name,size_t bytes,char kind)
{
    if(!object)return NULL;
    Class cls=object_getClass(object);Ivar field=class_getInstanceVariable(cls,name);
    const char *type=field?ivar_getTypeEncoding(field):NULL;
    ptrdiff_t offset=field?ivar_getOffset(field):-1;size_t size=class_getInstanceSize(cls);
    if(!type || *type!=kind || offset<0 || bytes>size || (size_t)offset>size-bytes)return NULL;
    return field;
}
static id creation_object(id object,const char *name)
{
    Ivar field=creation_ivar(object,name,sizeof(id),'@');
    return field?object_getIvar(object,field):nil;
}
static int creation_flag(id object,const char *name)
{
    Ivar field=creation_ivar(object,name,1,'B');
    if(!field)return -1;
    unsigned char value;memcpy(&value,(const char *)object+ivar_getOffset(field),1);
    return value?1:0;
}
static const char *creation_field_class(id object,const char *name)
{
    if(!object)return "nil";
    Ivar field=creation_ivar(object,name,sizeof(id),'@');
    return field?creation_class(object_getIvar(object,field)):"unavailable";
}
static int creation_field_present(id object,const char *name)
{
    if(!object)return 0;
    Ivar field=creation_ivar(object,name,sizeof(id),'@');
    return field?(object_getIvar(object,field)!=nil):-1;
}
static void creation_name(struct creation_message *message,const char *label,id value)
{
    if(!value){creation_append(message," %s=nil",label);return;}
    Class string=objc_lookUpClass("NSString");SEL kind=sel_registerName("isKindOfClass:");
    const char *args[]={"#"};
    if(!string || !creation_abi(class_getInstanceMethod(object_getClass(value),kind),"Bc",1,args) ||
       !((BOOL(*)(id,SEL,Class))objc_msgSend)(value,kind,string)) {
        creation_append(message," %s=unavailable",label);return;
    }
    char name[161]={0};
    if(!CFStringGetCString((CFStringRef)value,name,sizeof(name),kCFStringEncodingUTF8)) {
        creation_append(message," %s=unavailable",label);return;
    }
    // These callers supply only system layout/keyplane/file names, never text.
    for(const unsigned char *at=(const void *)name;*at;at++)
        if(!((*at>='A' && *at<='Z') || (*at<='z' && *at>='a') || (*at>='0' && *at<='9') ||
             strchr("_-.:;@()=,+/",*at))) {creation_append(message," %s=noncanonical",label);return;}
    creation_append(message," %s=%s",label,name);
}
static void creation_number(struct creation_message *message,id object,const char *name,char kind)
{
    Ivar field=creation_ivar(object,name,8,kind);
    if(!field){creation_append(message," %s=unavailable",name);return;}
    if(kind=='q') {
        long long value;memcpy(&value,(const char *)object+ivar_getOffset(field),8);
        creation_append(message," %s=%lld",name,value);
    } else {
        double value;memcpy(&value,(const char *)object+ivar_getOffset(field),8);
        creation_append(message," %s=%.3f",name,value);
    }
}
static void creation_view(struct creation_message *message,id view);
static void creation_traits(struct creation_message *message,id traits)
{
    creation_append(message," traits=%s",creation_class(traits));
    if(!traits)return;
    creation_number(message,traits,"_orientation",'q');
    creation_number(message,traits,"_keyboardWidth",'d');
    creation_number(message,traits,"_screenToNativeScaleRatio",'d');
    creation_number(message,traits,"_keyboardBarHeight",'d');
    creation_append(message," emulating=%d",creation_flag(traits,"_isEmulatingIdiom"));
    creation_number(message,traits,"_idiomToEmulate",'q');
    creation_view(message,creation_object(traits,"_screen"));
}
static void creation_decoder(struct creation_message *message,id factory)
{
    id decoder=creation_object(factory,"_decoder");
    creation_append(message," decoder=%s",creation_class(decoder));
    if(!decoder)return;
    creation_number(message,decoder,"_currentFileType",'q');
    id data=creation_object(decoder,"_serializedData");SEL length=sel_registerName("length");
    if(data && creation_abi(class_getInstanceMethod(object_getClass(data),length),"Q",0,NULL))
        creation_append(message," serializedBytes=%llu",((unsigned long long(*)(id,SEL))objc_msgSend)(data,length));
}
static void creation_view(struct creation_message *message,id view)
{
    creation_append(message," view=%s",creation_class(view));
    if(!view)return;
    Class cls=object_getClass(view);SEL bounds=sel_registerName("bounds"),window=sel_registerName("window");
    Method bm=class_getInstanceMethod(cls,bounds),wm=class_getInstanceMethod(cls,window);
    if(creation_abi(bm,KEYBOARD_RECT,0,NULL)) {
        CGRect rect;
#if defined(__x86_64__)
        // macOS host fixtures return this32-byte aggregate through stret.
        ((void(*)(CGRect *,id,SEL))objc_msgSend_stret)(&rect,view,bounds);
#elif defined(__arm64__) || defined(__aarch64__)
        // The deployed iOS ARM64 ABI returns CGRect in the SIMD registers.
        rect=((CGRect(*)(id,SEL))objc_msgSend)(view,bounds);
#else
        creation_append(message," bounds=unavailable");
        return;
#endif
        creation_append(message," bounds=%.1f,%.1f,%.1f,%.1f",rect.origin.x,rect.origin.y,rect.size.width,rect.size.height);
    }
    if(creation_abi(wm,"@",0,NULL)) {
        id attached=((id(*)(id,SEL))objc_msgSend)(view,window);
        creation_append(message," window=%s",creation_class(attached));
    }
}
static void creation_layout(struct creation_message *message,id layout)
{
    creation_append(message," layout=%s tree=%d keyplane=%s liveKeyplane=%s",creation_class(layout),
        creation_field_present(layout,"_keyplane"),creation_field_class(layout,"_keyplaneView"),
        creation_field_class(layout,"_liveKeyplaneView"));
    creation_name(message,"keyboardName",creation_object(layout,"_keyboardName"));
    creation_name(message,"keyplaneName",creation_object(layout,"_keyplaneName"));
    creation_view(message,layout);
}
static void creation_keyboard(struct creation_message *message,id keyboard)
{
    creation_append(message," initialized=%d updating=%d",creation_flag(keyboard,"m_initializationDone"),
        creation_flag(keyboard,"m_updatingLayout"));
    creation_view(message,keyboard);creation_layout(message,creation_object(keyboard,"m_layout"));
}
static void creation_set(struct creation_message *message,id set)
{
    creation_append(message," set=%s null=%d remote=%d custom=%d input=%s accessory=%s assistant=%s",
        creation_class(set),creation_flag(set,"_isNullInputView"),creation_flag(set,"_isRemoteKeyboard"),
        creation_flag(set,"_isCustomInputView"),creation_field_class(set,"_inputView"),
        creation_field_class(set,"_inputAccessoryView"),creation_field_class(set,"_inputAssistantView"));
    creation_view(message,creation_object(set,"_inputView"));
}
static void creation_line(struct creation_message *message,unsigned index,unsigned record,const char *phase)
{
    message->used=0;message->text[0]=0;
    creation_append(message,"Pong keyboard creation74: pid=%d %s.%s call=%u %s main=%u",
        getpid(),class_getName(creation[index].cls),sel_getName(creation[index].selector),record,phase,
        pthread_main_np()?1:0);
}

static void creation_update(id self,SEL selector)
{
    unsigned n=creation_record(0);bool main=pthread_main_np()!=0;struct creation_message message;
    if(n){creation_line(&message,0,n,"enter");if(main)creation_keyboard(&message,self);creation_emit(&message);}
    ((void(*)(id,SEL))creation_original(0))(self,selector);
    if(n){creation_line(&message,0,n,"leave");if(main)creation_keyboard(&message,self);creation_emit(&message);}
}
static id creation_keyboard_init(id self,SEL selector,CGRect rect,BOOL custom)
{
    unsigned n=creation_record(1);struct creation_message message;
    if(n){creation_line(&message,1,n,"enter");creation_append(&message," frame=%.1f,%.1f,%.1f,%.1f custom=%u",
        rect.origin.x,rect.origin.y,rect.size.width,rect.size.height,custom?1:0);creation_emit(&message);}
    id result=((id(*)(id,SEL,CGRect,BOOL))creation_original(1))(self,selector,rect,custom);
    if(n){creation_line(&message,1,n,"leave");creation_append(&message," result=%s",creation_class(result));
        if(pthread_main_np())creation_keyboard(&message,result);creation_emit(&message);}
    return result;
}
static Class creation_factory(id self,SEL selector,id mode,long long type,id traits)
{
    unsigned n=creation_record(2);struct creation_message message;
    if(n){creation_line(&message,2,n,"enter");creation_append(&message," modeClass=%s keyboardType=%lld traitsClass=%s",
        creation_class(mode),type,creation_class(traits));creation_emit(&message);}
    Class result=((Class(*)(id,SEL,id,long long,id))creation_original(2))(self,selector,mode,type,traits);
    if(pthread_main_np())pong_keyboard_creation18_install();
    if(n){creation_line(&message,2,n,"leave");creation_append(&message," result=%s",result?class_getName(result):"nil");creation_emit(&message);}
    return result;
}
static id creation_layout_init(id self,SEL selector,CGRect rect)
{
    unsigned n=creation_record(3);struct creation_message message;
    if(n){creation_line(&message,3,n,"enter");creation_append(&message," frame=%.1f,%.1f,%.1f,%.1f",
        rect.origin.x,rect.origin.y,rect.size.width,rect.size.height);creation_emit(&message);}
    id result=((id(*)(id,SEL,CGRect))creation_original(3))(self,selector,rect);
    if(n){creation_line(&message,3,n,"leave");creation_append(&message," result=%s",creation_class(result));
        if(pthread_main_np())creation_layout(&message,result);creation_emit(&message);}
    return result;
}
static void creation_layout_show(id self,SEL selector,id mode,id render,id input,id screen,id split,BOOL reload)
{
    if(pthread_main_np())pong_keyboard_creation18_install();
    unsigned n=creation_record(4);struct creation_message message;
    if(n){creation_line(&message,4,n,"enter");creation_append(&message," modeClass=%s reload=%u",creation_class(mode),reload?1:0);
        if(pthread_main_np())creation_traits(&message,screen);creation_emit(&message);}
    ((void(*)(id,SEL,id,id,id,id,id,BOOL))creation_original(4))(self,selector,mode,render,input,screen,split,reload);
    if(n){creation_line(&message,4,n,"leave");if(pthread_main_np())creation_layout(&message,self);creation_emit(&message);}
}
static void creation_layout_window(id self,SEL selector)
{
    unsigned n=creation_record(5);struct creation_message message;
    ((void(*)(id,SEL))creation_original(5))(self,selector);
    if(n){creation_line(&message,5,n,"leave");if(pthread_main_np())creation_layout(&message,self);creation_emit(&message);}
}
static id creation_input_set(id self,SEL selector,id responder)
{
    unsigned n=creation_record(6);struct creation_message message;
    if(n){creation_line(&message,6,n,"enter");creation_append(&message," responderClass=%s",creation_class(responder));creation_emit(&message);}
    id result=((id(*)(id,SEL,id))creation_original(6))(self,selector,responder);
    if(n){creation_line(&message,6,n,"leave");if(pthread_main_np())creation_set(&message,result);
        else creation_append(&message," result=%s",creation_class(result));creation_emit(&message);}
    return result;
}
static BOOL creation_scene_show(id self,SEL selector,id set,id placement,id scene)
{
    unsigned n=creation_record(7);struct creation_message message;
    if(n){creation_line(&message,7,n,"enter");if(pthread_main_np())creation_set(&message,set);
        creation_append(&message," placementClass=%s sceneClass=%s",creation_class(placement),creation_class(scene));creation_emit(&message);}
    BOOL result=((BOOL(*)(id,SEL,id,id,id))creation_original(7))(self,selector,set,placement,scene);
    if(n){creation_line(&message,7,n,"leave");creation_append(&message," original=%u",result?1:0);creation_emit(&message);}
    return result;
}
/* 97 evidence: SpringBoard's input view set has input=nil and no layout/factory
 * probe fired. Report the live input-mode/layout state without changing it.
 * Bounded, main-thread, identifiers only; never text, credentials or surface
 * content. */
static void creation_modes_report(const char *where)
{
    static unsigned reports;
    if(!pthread_main_np())return;
    if(__atomic_fetch_add(&reports,1,__ATOMIC_RELAXED)>=6)return;
    struct creation_message message;creation_append(&message,"Pong keyboard modes: pid=%d at=%s",getpid(),where);
    Class controller=objc_lookUpClass("UIKeyboardInputModeController");
    creation_append(&message," controller=%d",controller?1:0);
    if(controller) {
        SEL shared=sel_registerName("sharedInputModeController");
        const char *noargs[]={NULL};
        if(creation_abi(class_getClassMethod(controller,shared),"@",0,noargs)) {
            id instance=((id(*)(id,SEL))objc_msgSend)((id)controller,shared);
            creation_append(&message," shared=%d",instance?1:0);
            if(instance) {
                SEL mode=sel_registerName("currentInputMode");
                if(creation_abi(class_getInstanceMethod(controller,mode),"@",0,noargs)) {
                    id value=((id(*)(id,SEL))objc_msgSend)(instance,mode);
                    creation_append(&message," modeClass=%s",creation_class(value));
                    if(!value)creation_append(&message," currentMode=nil");
                    else {SEL identifier=sel_registerName("identifier");
                        if(creation_abi(class_getInstanceMethod(object_getClass(value),identifier),"@",0,noargs))
                            creation_name(&message,"mode",((id(*)(id,SEL))objc_msgSend)(value,identifier));
                        else creation_append(&message," modeIdentifier=unavailable");}
                }
                SEL supported=sel_registerName("currentSupportedInputModes");
                if(creation_abi(class_getInstanceMethod(controller,supported),"@",0,noargs)) {
                    id list=((id(*)(id,SEL))objc_msgSend)(instance,supported);
                    creation_append(&message," supportedClass=%s",creation_class(list));
                    if(list && CFGetTypeID((CFTypeRef)list)==CFArrayGetTypeID())
                        creation_append(&message," supportedCount=%ld",(long)CFArrayGetCount((CFArrayRef)list));
                }
                SEL pref=sel_registerName("inputModePreference");
                if(creation_abi(class_getInstanceMethod(controller,pref),"@",0,noargs))
                    creation_name(&message,"preference",((id(*)(id,SEL))objc_msgSend)(instance,pref));
                SEL fromId=sel_registerName("keyboardInputModeWithIdentifier:");
                if(creation_abi(class_getClassMethod(controller,fromId),"@",1,(const char *const[]){"@"})) {
                    id current=((id(*)(id,SEL))objc_msgSend)(instance,mode);
                    if(current) {
                        SEL identifier=sel_registerName("identifier");
                        id text=creation_abi(class_getInstanceMethod(object_getClass(current),identifier),"@",0,noargs)
                            ? ((id(*)(id,SEL))objc_msgSend)(current,identifier):nil;
                        if(text) creation_append(&message," roundtrip=%d",
                            ((id(*)(id,SEL,id))objc_msgSend)((id)controller,fromId,text)?1:0);
                    }
                }
            }
        }
    }
    Class impl=objc_lookUpClass("UIKeyboardImpl");
    creation_append(&message," impl=%d",impl?1:0);
    if(impl) {
        SEL active=sel_registerName("activeInstance");
        const char *noargs[]={NULL};
        if(creation_abi(class_getClassMethod(impl,active),"@",0,noargs)) {
            id keyboard=((id(*)(id,SEL))objc_msgSend)((id)impl,active);
            creation_append(&message," active=%d",keyboard?1:0);
            if(keyboard) {
                creation_append(&message," layoutClass=%s",creation_field_class(keyboard,"_layout"));
                creation_append(&message," inputModeClass=%s",creation_field_class(keyboard,"inputMode"));
                creation_append(&message," isMinimized=%d",creation_flag(keyboard,"_isMinimized"));
            }
        }
    }
    Class factory=objc_lookUpClass("TUIKeyboardLayoutFactory");
    creation_append(&message," factory=%d",factory?1:0);
    creation_append(&message," layoutStar=%d",objc_lookUpClass("UIKeyboardLayoutStar")?1:0);
    creation_emit(&message);
}
static unsigned long long creation_change_set(id self,SEL selector,id set)
{
    unsigned n=creation_record(8);struct creation_message message;
    if(n){creation_line(&message,8,n,"enter");if(pthread_main_np())creation_set(&message,set);creation_emit(&message);}
    if(pthread_main_np())creation_modes_report("changeToInputViewSet");
    unsigned long long result=((unsigned long long(*)(id,SEL,id))creation_original(8))(self,selector,set);
    if(n){creation_line(&message,8,n,"leave");creation_append(&message," original=%llu",result);creation_emit(&message);}
    return result;
}
static void creation_finish_layout(id self,SEL selector,id arguments)
{
    unsigned n=creation_record(9);struct creation_message message;
    ((void(*)(id,SEL,id))creation_original(9))(self,selector,arguments);
    if(n){creation_line(&message,9,n,"leave");if(pthread_main_np())creation_keyboard(&message,self);creation_emit(&message);}
}
static void creation_keyboard_window(id self,SEL selector)
{
    unsigned n=creation_record(10);struct creation_message message;
    ((void(*)(id,SEL))creation_original(10))(self,selector);
    if(n){creation_line(&message,10,n,"leave");if(pthread_main_np())creation_keyboard(&message,self);creation_emit(&message);}
}
static id creation_suffix(id self,SEL selector,CGSize dimensions)
{
    unsigned n=creation_record(11);struct creation_message message;
    id result=((id(*)(id,SEL,CGSize))creation_original(11))(self,selector,dimensions);
    if(n){creation_line(&message,11,n,"leave");creation_append(&message," dimensions=%.3f,%.3f",dimensions.width,dimensions.height);
        if(pthread_main_np()){creation_decoder(&message,self);creation_name(&message,"suffix",result);}creation_emit(&message);}
    return result;
}
static id creation_layout_file(id self,SEL selector)
{
    unsigned n=creation_record(12);struct creation_message message;
    id result=((id(*)(id,SEL))creation_original(12))(self,selector);
    if(n){creation_line(&message,12,n,"leave");if(pthread_main_np())creation_name(&message,"layoutFile",result);creation_emit(&message);}
    return result;
}
static id creation_named_keyboard(id self,SEL selector,id name,id cache)
{
    unsigned n=creation_record(13);struct creation_message message;
    if(n){creation_line(&message,13,n,"enter");if(pthread_main_np()){creation_name(&message,"requestedName",name);creation_decoder(&message,self);}creation_emit(&message);}
    id result=((id(*)(id,SEL,id,id))creation_original(13))(self,selector,name,cache);
    if(n){creation_line(&message,13,n,"leave");creation_append(&message," result=%s",creation_class(result));
        if(pthread_main_np())creation_decoder(&message,self);creation_emit(&message);}
    return result;
}

void pong_keyboard_creation18_install(void)
{
    if(!pthread_main_np())return;
    bool first=!creation_installed;
    creation_installed=true;
    const struct {const char *cls,*selector,*result;bool meta;unsigned count;const char *arguments[6];IMP wrapper;} specs[]={
        {"UIKeyboardImpl","updateLayout","v",false,0,{NULL},(IMP)creation_update},
        {"UIKeyboardImpl","initWithFrame:forCustomInputView:","@",false,2,{KEYBOARD_RECT,"Bc"},(IMP)creation_keyboard_init},
        {"UIKeyboardImpl","layoutClassForInputMode:keyboardType:screenTraits:","#",true,3,{"@","q","@"},(IMP)creation_factory},
        {"UIKeyboardLayoutStar","initWithFrame:","@",false,1,{KEYBOARD_RECT},(IMP)creation_layout_init},
        {"UIKeyboardLayoutStar","showKeyboardWithInputMode:renderConfig:inputTraits:screenTraits:splitTraits:reload:","v",false,6,{"@","@","@","@","@","Bc"},(IMP)creation_layout_show},
        {"UIKeyboardLayoutStar","didMoveToWindow","v",false,0,{NULL},(IMP)creation_layout_window},
        {"UIKeyboardSceneDelegate","_inputViewsForResponder:","@",false,1,{"@"},(IMP)creation_input_set},
        {"UIKeyboardSceneDelegate","_showKeyboardWindowForInputViewSet:placement:windowScene:","Bc",false,3,{"@","@","@"},(IMP)creation_scene_show},
        {"UIInputWindowController","changeToInputViewSet:","Q",false,1,{"@"},(IMP)creation_change_set},
        {"UIKeyboardImpl","finishLayoutChangeWithArguments:","v",false,1,{"@"},(IMP)creation_finish_layout},
        {"UIKeyboardImpl","didMoveToWindow","v",false,0,{NULL},(IMP)creation_keyboard_window},
        {"TUIKeyboardLayoutFactory","keyboardSuffixForScreenDimensions:","@",false,1,{KEYBOARD_SIZE},(IMP)creation_suffix},
        {"TUIKeyboardLayoutFactory","layoutsFileNameForDeviceClass","@",true,0,{NULL},(IMP)creation_layout_file},
        {"TUIKeyboardLayoutFactory","keyboardWithName:inCache:","@",false,2,{"@","@"},(IMP)creation_named_keyboard}
    };
    _Static_assert(sizeof(specs)/sizeof(specs[0])==sizeof(creation)/sizeof(creation[0]),"creation table size");
    unsigned installed=0,added=0;
    for(unsigned q=0;q<sizeof(specs)/sizeof(specs[0]);q++) {
        if(creation_original(q)){installed++;continue;}
        Class cls=objc_lookUpClass(specs[q].cls);if(!cls)continue;
        if(specs[q].meta)cls=object_getClass((id)cls);
        unsigned count=0;Method *methods=class_copyMethodList(cls,&count);
        for(unsigned i=0;i<count;i++) {
            if(strcmp(sel_getName(method_getName(methods[i])),specs[q].selector) ||
               !creation_abi(methods[i],specs[q].result,specs[q].count,specs[q].arguments))continue;
            creation[q].cls=cls;creation[q].selector=method_getName(methods[i]);
            __atomic_store_n(&creation[q].original,method_getImplementation(methods[i]),__ATOMIC_RELEASE);
            method_setImplementation(methods[i],specs[q].wrapper);installed++;added++;break;
        }
        free(methods);
    }
    if(first || added)fprintf(stderr,"Pong keyboard creation74: installed=%u result=%s added=%u original calls/arguments/results retained, max=%u per entry; no input text\n",
        installed,installed?"PASS":"SKIP",added,KEYBOARD_CREATION_LIMIT);
}
