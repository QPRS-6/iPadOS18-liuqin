"""22H374 arm64 native controller routing. Not an iPadOS27 offset transplant."""
from pathlib import Path
import importlib.util,struct

UUID='D3FB9884-D7FB-3B11-A807-5CFB1845C2C8'
TRACE_COUNTERS=0x1009e4120
MODEL_POINTER=0x1009dae90
MODEL_PRODUCT=0x2e0

def branch(pc,target,link=False):
    delta=target-pc
    if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('AArch64 branch range')
    return (0x94000000 if link else 0x14000000)|((delta//4)&0x3ffffff)

def adrp(pc,reg,target):
    pages=((target&~4095)-(pc&~4095))//4096
    if not -(1<<20)<=pages<(1<<20):raise ValueError('ADRP range')
    pages&=(1<<21)-1
    return 0x90000000|((pages&3)<<29)|((pages>>2)<<5)|reg

def add(reg,base,offset):return 0x91000000|(offset<<10)|(base<<5)|reg
def mov(reg,value):return 0x52800000|(value<<5)|reg

def patch_native(image,reader):
    if image.uuid!=UUID or image.cpu!=0x100000c or image.subtype!=0:raise ValueError('Exact22H374 ordinary arm64 daemon required')
    data=bytearray(image.data);changes=[];cursor=0x100957aa8;limit=0x100958000
    if any(data[image.offset(cursor):image.offset(limit-1)+1]):raise ValueError('Native executable padding occupied')
    for s in image.sections:
        if max(cursor,s['address'])<min(limit,s['address']+s['size']):raise ValueError('Trampoline overlaps native section')
    def word(a):return struct.unpack_from('<I',data,image.offset(a))[0]
    def put(a,words,expected=None):
        if expected is not None and [word(a+4*i) for i in range(len(expected))]!=expected:raise ValueError('Native instruction guard mismatch: '+hex(a))
        for i,w in enumerate(words):struct.pack_into('<I',data,image.offset(a+4*i),w)
        changes.append(dict(address=hex(a),expected=[hex(w) for w in expected] if expected else None,words=[hex(w) for w in words]))
    def emit(words):
        nonlocal cursor
        a=cursor
        if a+4*len(words)>limit:raise ValueError('Trampoline capacity')
        out=[w(a+4*i) if callable(w) else w for i,w in enumerate(words)];put(a,out);cursor+=4*len(words);return a
    entries={}
    # All native product constructors share this exact one-instruction thunk.
    a=emit([mov(1,0),lambda pc:branch(pc,0x10032b17c)])
    put(0x10032bc44,[branch(0x10032bc44,a)],[branch(0x10032bc44,0x10032b17c)])
    entries['controller_type0']=hex(a)
    # This build's getter caches kern.hv_vmm_present, and both availability and
    # chip initialization use it. The port has a real, identity-gated controller,
    # not a virtual HCI backend. Change only this daemon's selector, not the guest
    # sysctl/DT or any genuine initialization result.
    put(0x1003e5614,[mov(0,0),0xd65f03c0],[adrp(0x1003e5614,8,0x1009db000),0xf9421908])
    entries['physical_controller_selector']=hex(0x1003e5614)
    # A product absent from the Apple model table takes the final constructor,
    # rather than leaving the model pointer null (observed79 startup SIGSEGV).
    put(0x10037bb40,[0xd503201f],[0x340002a0])
    # Preserve the1..6000 native bound and add exactly zero registration.
    a=emit([lambda pc:0x35000000|((8//4)<<5),lambda pc:branch(pc,0x1002229c0),0x51000409,lambda pc:branch(pc,0x1002229b4)])
    put(0x1002229b0,[branch(0x1002229b0,a)],[0x51000409]);entries['registration0']=hex(a)
    # At this site the native frame and chip/context stores already exist.
    # Type0 clears only vendor operation storage; standard HCI stays native.
    start=cursor
    words=[None,lambda pc:adrp(pc,0,0x1009cde70),add(0,0,0xe70),mov(1,0x948),lambda pc:branch(pc,0x10074f160,True),
        lambda pc:adrp(pc,0,0x1009cde70),add(0,0,0xe70),
        lambda pc:adrp(pc,1,0x1009632a0),add(1,1,0x2a0),
        # Owner92: no0x2041 reached HCI because this whole ops table is zeroed
        # for type0. The build's own static table holds the standard scan
        # submit callbacks at+0x100/+0x108; restore only those two standard
        # HCI operations, leaving every vendor backend slot clear.
        0xf9408022,0xf9008002,0xf9408423,0xf9008403,
        lambda pc:branch(pc,0x100222988),0x51000408,lambda pc:branch(pc,0x1002228ac)]
    words[0]=lambda pc:0x35000000|(((start+4*(len(words)-2)-pc)//4)<<5)
    a=emit(words);put(0x1002228a8,[branch(0x1002228a8,a)],[0x51000408]);entries['vendor0']=hex(a)
    if struct.unpack_from('<QQ',image.data,image.offset(0x1009633a0))!=(0x1000000006df9c,0x100000000854c4):
        raise ValueError('Native standard scan ops table guard')
    # Each path retains its original generic callback global store. Skip only
    # the missing vendor extension for zero, not the normal callback registration.
    for name,site,epilogue,displaced in (
        ('callback0',0x100222bac,0x100222c64,0x51000509),
        ('callback1',0x100222ca0,0x100222d24,0x51000509),
        ('callback2',0x100222d60,0x100222dc0,0x51000508)):
        start=cursor;a=emit([lambda pc:0x35000008|((8//4)<<5),lambda pc,e=epilogue:branch(pc,e),displaced,lambda pc,s=site:branch(pc,s+4)])
        put(site,[branch(site,a)],[displaced]);entries[name]=hex(a)
    # 79's actual console exposed FindLocalMacAddress's fprintf output. The
    # owner's address must remain usable by HCI/DT/key storage but never become
    # diagnostic output. Guard only this build's address/key-state log calls;
    # do not alter parsers, keychain results, initialization or failure branches.
    private_logs=[]
    def private_log(site,target,literal,prefix):
        at=image.offset(literal);expected=prefix.encode()
        if image.data[at:at+len(expected)]!=expected:raise ValueError('Native private-log literal guard: '+hex(literal))
        put(site,[0xd503201f],[branch(site,target,True)])
        private_logs.append(dict(address=hex(site),kind='nop',literal=hex(literal),prefix=prefix))
    for site,literal,prefix in (
        (0x1000b1288,0x100896b16,'Local address from ENV:'),
        (0x1000b1c48,0x100896c2f,'FindLocalMacAddress Local address from plist:'),
        (0x1000b1d7c,0x100896c61,'FindLocalMacAddress Storing local address to plist:'),
        (0x1000b24ec,0x100896d54,'%s: Got local-mac-address:'),
        (0x10028bac0,0x10089ae16,'Providing the local device address of'),
        (0x10034a8b4,0x10089ae16,'Providing the local device address of'),
        (0x10034ada8,0x1008b58e6,'DeviceTree address ='),
        (0x1003c3710,0x1008bd170,'statedump:       Local address:'),
        (0x1004c7b74,0x1008e5ed5,'statedump: Local device is'),
        (0x1004c7c00,0x1008e5f0f,'Init Core Stack - {localname:'),
        (0x10051d2e4,0x1008f20ec,'statedump: IDS Multi users dictionary for local address:'),
        (0x10051eab8,0x1008f23ac,"MUC: Use local host's random static address:"),
        (0x10051eb3c,0x1008f23dc,"MUC: Use local host's public address:"),
        (0x10051f3c4,0x1008f24e8,'MUC - pairing with local host address'),
        (0x100520688,0x1008f26b4,'MUC - local public address'),
        (0x1005f791c,0x10090db07,'Writing local address'),
        (0x1006453f8,0x100918abd,'Local LE Connectable address is now'),
        (0x100645454,0x100918aee,'Local LE Non-Connectable address is now'),
        (0x1006454b4,0x100918b23,'Local LE Non-Connectable Secondary address is now'),
        (0x10066047c,0x10092083a,'statedump: Local address'),
        (0x1006a8b6c,0x10092e05c,'TTD: created temporary IRK='),
        (0x1006a943c,0x10092e433,'IRK='),
        (0x1006c05a8,0x100931f1b,'statedump: IRK='),
        (0x1006dc0e8,0x100935cd3,'%s: DeviceTree address ='),
        (0x1006dc23c,0x100935d04,'%s Local address from plist:'),
        (0x1006dc330,0x100935d25,'%s Storing local address to plist:')):
        private_log(site,0x10074f010,literal,prefix)
    for site,literal,prefix in (
        (0x1000b1c7c,0x1007dff27,'FindLocalMacAddress Local address from plist:'),
        (0x1000b1d9c,0x1007dff77,'FindLocalMacAddress Storing local address to plist:'),
        (0x1000b2508,0x1007e0089,'%s: Got local-mac-address:')):
        private_log(site,0x10074f5bc,literal,prefix)
    for site,literal,prefix in (
        (0x10019fb00,0x10080bda4,'Local device address is:'),
        (0x100245274,0x10082a5de,'gLocalConnectableAddress.Type:')):
        private_log(site,0x100280db8,literal,prefix)
    # The ENV fprintf shares a call site with non-address error messages; skip
    # that call only on the ENV-address branch, preserving the shared errors.
    expected=b'Local address from ENV:%'
    if image.data[image.offset(0x1007dfdc4):image.offset(0x1007dfdc4)+len(expected)]!=expected:raise ValueError('Native ENV log guard')
    put(0x1000b12a4,[branch(0x1000b12a4,0x1000b145c)],[branch(0x1000b12a4,0x1000b1458)])
    private_logs.append(dict(address=hex(0x1000b12a4),kind='branch_skip',stop=hex(0x1000b145c)))
    # Retain the genuine unexpected-address fault, without serializing its
    # address value. Ordinary arm64 printf ignores all unused variadic args.
    message=b'Pong Bluetooth81: local address unexpectedly set; value withheld\n\0'
    literal=cursor+7*4
    a=emit([0xa9bf7bfd,0x910003fd,lambda pc:adrp(pc,0,literal),add(0,0,literal&4095),lambda pc:branch(pc,0x10075015c,True),0xa8c17bfd,0xd65f03c0])
    padded=message.ljust((len(message)+3)&~3,b'\0');emit(struct.unpack('<'+'I'*(len(padded)//4),padded))
    expected=b'Local address is not zero!!'
    if image.data[image.offset(0x1008b245b):image.offset(0x1008b245b)+len(expected)]!=expected:raise ValueError('Native address fault guard')
    put(0x10032730c,[branch(0x10032730c,a,True)],[branch(0x10032730c,0x10074f004,True)])
    entries['private_fault_logger']=hex(a)
    private_logs.append(dict(address=hex(0x10032730c),kind='redacted_fault',message=message[:-1].decode()))
    # Actual82 now exchanges HCI, then exits(1), but OI log routing did not expose
    # the failing init step. Preserve the original status/arguments and native
    # failure branch; print only nonzero status with its trusted internal label.
    # Darwin arm64 printf variadic arguments live on the stack, not in x1/x2.
    start=cursor;literal=start+20*4;tail=start+18*4
    a=emit([lambda pc:0x34000000|(((tail-pc)//4)<<5),0xd10183ff,
        0xa9057bfd,0xa90107e0,0xa9020fe2,0xa90317e4,0xa9041fe6,0xa90003e1,
        lambda pc:adrp(pc,0,literal),add(0,0,literal&4095),lambda pc:branch(pc,0x10075015c,True),
        0xa94107e0,0xa9420fe2,0xa94317e4,0xa9441fe6,0xa9457bfd,0x910183ff,0xd503201f,
        0xd10103ff,lambda pc:branch(pc,0x1001c1f80)])
    message=b'Pong Bluetooth83: native init step %.64s status=%u; failure preserved\n\0'
    padded=message.ljust((len(message)+3)&~3,b'\0');emit(struct.unpack('<'+'I'*(len(padded)//4),padded))
    put(0x1001c1f7c,[branch(0x1001c1f7c,a)],[0xd10103ff]);entries['init_failure_trace']=hex(a)
    # The common main error return retains return1 and the native cleanup. Only
    # x20's low status bits are printed, never the local address/name strings.
    start=cursor;literal=start+10*4
    a=emit([0xd10083ff,0xa9017bfd,0xf90003f4,lambda pc:adrp(pc,0,literal),add(0,0,literal&4095),
        lambda pc:branch(pc,0x10075015c,True),0xa9417bfd,0x910083ff,mov(0,1),lambda pc:branch(pc,0x1000b183c)])
    message=b'Pong Bluetooth83: native main return1 lastLocalStatus=%u; no success fabricated\n\0'
    padded=message.ljust((len(message)+3)&~3,b'\0');emit(struct.unpack('<'+'I'*(len(padded)//4),padded))
    put(0x1000b1838,[branch(0x1000b1838,a)],[mov(0,1)]);entries['main_failure_trace']=hex(a)
    # Owner83 proves opcode0x0c5b returns HCI Unknown Command(1), translated by
    # this exact native callback to OI status701. Erroneous data reporting is
    # an optional SCO/speech feature (Linux hci_sync.c gates it on command and
    # LMP support); it is not keyboard/pairing capability. Skip only that
    # explicit unsupported result. Other errors and successful callbacks take
    # the original prologue/body; no HCI response or feature bit is fabricated.
    start=cursor;literal=start+13*4;original=start+11*4
    a=emit([0x7100001f|(701<<10),lambda pc:0x54000001|(((original-pc)//4)<<5),
        0xa9bf7bfd,lambda pc:adrp(pc,0,literal),add(0,0,literal&4095),lambda pc:branch(pc,0x10075015c,True),
        0xa8c17bfd,mov(0,0),lambda pc:adrp(pc,1,0x100812e02),add(1,1,0xe02),lambda pc:branch(pc,0x1001c1f7c),
        0xd100c3ff,lambda pc:branch(pc,0x1001c51d0)])
    message=b'Pong Bluetooth84: optional0c5b SKIP explicit HCI Unknown Command; feature not enabled, response not forged\n\0'
    padded=message.ljust((len(message)+3)&~3,b'\0');emit(struct.unpack('<'+'I'*(len(padded)//4),padded))
    put(0x1001c51cc,[branch(0x1001c51cc,a)],[0xd100c3ff]);entries['optional_err_data_unsupported']=hex(a)
    # Owner84: controller returns Command Disallowed(12) for old 0x2007 LE Read
    # Advertising Channel TX Power; the core spec forbids this legacy read once
    # extended advertising is in use, and Linux gates it the same way. This
    # build already has Apple's own "Skip Reading ... will wait" branch; take it
    # unconditionally so no value is read or invented. Empty advertising TX
    # power remains unknown; no capability or HCI response is fabricated.
    put(0x10061f518,[0xd503201f],[0x710006bf])
    put(0x10061f51c,[branch(0x10061f51c,0x10061f520)],[0x540001a1])
    entries['legacy_adv_tx_power_skip']=hex(0x10061f518)
    # Owner89 still uses legacy scanning. Do not infer that the tested type0
    # helper path/values were actually used. Record its real init/query/model/
    # setter scalars, with at most two lines per phase per daemon process.
    # The counter storage is guarded zero-fill padding AFTER all __DATA sections,
    # inside the existing writable segment, not a guessed native structure field.
    data_segment=None;at=32
    for _ in range(struct.unpack_from('<I',image.data,16)[0]):
        command,size=struct.unpack_from('<II',image.data,at)
        if command==0x19 and image.data[at+8:at+24].rstrip(b'\0')==b'__DATA':
            va,vm,offset,file_size,maxprot,initprot=struct.unpack_from('<QQQQII',image.data,at+24)
            data_segment=(va,vm,file_size,maxprot,initprot)
        at+=size
    if data_segment!=(0x1009ac000,0x3c000,0x18000,3,3):raise ValueError('Native trace writable segment guard')
    if TRACE_COUNTERS<0x1009ac000+0x18000 or TRACE_COUNTERS+16>0x1009e8000:
        raise ValueError('Native trace zero-fill range guard')
    if any(max(TRACE_COUNTERS,s['address'])<min(TRACE_COUNTERS+16,s['address']+s['size']) for s in image.sections):
        raise ValueError('Native trace counter padding occupied')
    def pair(load,first,second,offset):
        return (0xa9400000 if load else 0xa9000000)|((offset//8)<<15)|(second<<10)|(31<<5)|first
    start=cursor;ops=[0xd10403ff]
    saved=[(n,n+1,0x40+8*n) for n in range(0,18,2)]+[(18,30,0xd0)]
    ops += [pair(False,a,b,o) for a,b,o in saved]+[0xd53b4211,0xf90073f1]
    labels={}
    def jump(label):return lambda pc:branch(pc,start+4*labels[label])
    def cond(label,condition):return lambda pc:0x54000000|((((start+4*labels[label]-pc)//4)&0x7ffff)<<5)|condition
    def cbz(label,reg):return lambda pc:0x34000000|((((start+4*labels[label]-pc)//4)&0x7ffff)<<5)|reg
    ops += [0xf1000c1f,cond('unwind',8),lambda pc:adrp(pc,13,TRACE_COUNTERS),add(13,13,TRACE_COUNTERS&4095),0x8b0009ad,mov(15,4)]
    labels['retry']=len(ops)
    ops += [0x885ffdac,0x7100099f,cond('denied',2),0x1100058c,0x880efdac,cbz('print',14),0x710005ef,cond('retry',1)]
    labels['denied']=len(ops);ops += [0xd5033f5f,jump('unwind')]
    labels['print']=len(ops)
    ops += [lambda pc:adrp(pc,8,0x1009cdd60),0xb9400000|((0xd60//4)<<10)|(8<<5)|8,
        0xf90003e0,0xf90007e8,0xf9000be1,0xf9000fe2,
        lambda pc:adrp(pc,8,0x1009e34bd),0x39400000|(0x4bd<<10)|(8<<5)|8,0xf90013e8,
        lambda pc:adrp(pc,8,0x1009db058),0x39400000|(0x58<<10)|(8<<5)|8,0xf90017e8,
        lambda pc:adrp(pc,8,0x1009d8a06),0x39400000|(0xa06<<10)|(8<<5)|8,0xf9001be8,
        lambda pc:adrp(pc,0,literal),lambda pc:add(0,0,literal&4095),lambda pc:branch(pc,0x10075015c,True)]
    labels['unwind']=len(ops)
    ops += [0xf94073f1,0xd51b4211]+[pair(True,a,b,o) for a,b,o in saved]+[0x910403ff,0xd65f03c0]
    literal=start+4*len(ops);trace=emit(ops)
    message=b'Pong Bluetooth90: scan phase=%u registered=%u arg=%u complete=%u cmd37=%02x upper=%u lower=%u\n\0'
    padded=message.ljust((len(message)+3)&~3,b'\0');emit(struct.unpack('<'+'I'*(len(padded)//4),padded))
    entries['scan_execution_trace']=hex(trace)
    # The helper sizes below are fixed; forward targets stay inside guarded
    # executable padding. Wrappers preserve x0..x18/LR/NZCV and original args.
    future=cursor+(14+11+11+11)*4
    future_model=future+19*4;future_setter=future_model+5*4;future_init=future_setter+8*4
    wrappers={}
    for phase,arg,continuation in ((1,0x2a1403e1,0x1001c6b4c),(3,0x2a0803e1,future_model),
                                    (2,0x2a0003e1,future_setter),(0,mov(1,0),future_init)):
        words=[0xd10083ff,pair(False,0,1,0),pair(False,2,30,0x10),arg,mov(0,phase),mov(2,0)]
        if phase==1:words += [0xb4000055,mov(2,1)]
        words += [lambda pc:branch(pc,trace,True),pair(True,2,30,0x10),pair(True,0,1,0),0x910083ff]
        if phase==1:words += [0xaa1403e0]
        words += [lambda pc,target=continuation:branch(pc,target)]
        wrappers[phase]=emit(words)
    if cursor!=future:raise ValueError('Native trace forward-target guard')
    # Owner87: real supported-commands[37]=ff, yet type0 selects legacy0x200b
    # and receives Command Disallowed12. In THIS build the native0xd7/0xd8
    # descriptors are standard0x2041/0x2042, selected by ExtendedScanning
    # (9d8a06/9db058), NOT the distinct multicore backend flag9d8a05. Publish
    # only the direct standard-command selector from a successful,
    # complete native Read Supported Commands result. x21 is nonnull only after
    # the native parser validated/copied all64 bytes; x20 retains its status.
    # A failed/short query clears both standard scan caches, preserves the
    # original callback/status and never reads old bitmap data. Nonzero native
    # models retain their original path and caches. Owner90 proves that the
    # vendor registration becomesffff before the successful callback, so it is
    # NOT the stable product identity. Use the same model pointer/product field
    # as this build's native singleton getter, without altering that registration.
    if [word(a) for a in (0x100007880,0x100007884)]!=[0xf0004e88,0xf9474900]:
        raise ValueError('Native stable model pointer getter guard')
    if word(0x10032ce54)!=0xb942e008:raise ValueError('Native model product field guard')
    def model_load():
        return [lambda pc:adrp(pc,8,MODEL_POINTER),0xf9400000|(((MODEL_POINTER&4095)//8)<<10)|(8<<5)|8]
    product_load=0xb9400000|((MODEL_PRODUCT//4)<<10)|(8<<5)|8
    if [word(0x1001c6b94+4*i) for i in range(4)]!=[0xb00040f7,0x911262f7,0xad010ee2,0xad0006e0]:
        raise ValueError('Native supported-command copy/table guard')
    for address,expected in ((0x1009c19a0,b'\x04\x00\x41\x20\x20\x25\x00\x01'),
                             (0x1009c19b8,b'\x04\x00\x42\x20\x40\x25\x06\x00')):
        if image.data[image.offset(address):image.offset(address)+8]!=expected:
            raise ValueError('Native standard scan descriptor guard: '+hex(address))
    start=cursor;store=start+13*4;finish=start+17*4
    a=emit([*model_load(),lambda pc:0xb4000008|(((finish-pc)//4)<<5),product_load,
        lambda pc:0x35000008|(((finish-pc)//4)<<5),mov(9,0),
        lambda pc:0x35000014|(((store-pc)//4)<<5),lambda pc:0xb4000015|(((store-pc)//4)<<5),
        lambda pc:adrp(pc,8,0x1009e34bd),0x39400000|(0x4bd<<10)|(8<<5)|8,
        0x121b0508,0x7101811f,
        0x1a9f17e9,lambda pc:adrp(pc,8,0x1009db058),0x39000000|(0x58<<10)|(8<<5)|9,
        lambda pc:adrp(pc,8,0x1009d8a06),0x39000000|(0xa06<<10)|(8<<5)|9,
        lambda pc:branch(pc,wrappers[1]),0xd503201f])
    put(0x1001c6b48,[branch(0x1001c6b48,a)],[0xaa1403e0]);entries['standard_scan_capability_result']=hex(a)
    # The same build's0x848 model getter rejects type0 by product table. For exactly
    # zero use the above real-result cache, keeping all other product branches.
    # Do not enable Apple VSC scanning or write any HCI capability bitmap bit.
    start=cursor
    a=emit([lambda pc:0x35000008|(((start+4*4-pc)//4)<<5),lambda pc:adrp(pc,8,0x1009db058),
        0x39400000|(0x58<<10)|(8<<5),0xd65f03c0,lambda pc:branch(pc,0x10032ce5c)])
    if a!=future_model:raise ValueError('Native trace model target guard')
    put(0x10032ce58,[branch(0x10032ce58,wrappers[3])],[0x34000228]);entries['standard_scan_model0']=hex(a)
    # Owner88 still sends legacy0x200b. Signed88 instruction composition proves
    # a missing ordering guard: the later core-stack setter can replace the
    # query-published lower selector with an earlier false config snapshot.
    # For exactly stable model type0 that selector belongs to the validated query, not the
    # cached config argument. Do not enable it unconditionally or alter w0.
    start=cursor
    a=emit([*model_load(),lambda pc:0xb4000008|(((start+6*4-pc)//4)<<5),product_load,
        lambda pc:0x35000008|(((start+6*4-pc)//4)<<5),0xd65f03c0,
        lambda pc:adrp(pc,8,0x1009d8a06),lambda pc:branch(pc,0x100258600)])
    if a!=future_setter:raise ValueError('Native trace setter target guard')
    put(0x1002585fc,[branch(0x1002585fc,wrappers[2])],[0x90003c08]);entries['standard_scan_config_setter0']=hex(a)
    # A new real native HCI initialization also resets the command table. Clear
    # the type0 scan result at that same boundary so a timed-out/absent query
    # cannot reuse an earlier session's support. Other products and the original
    # table initialization instruction/body remain unchanged.
    start=cursor;original=start+9*4
    a=emit([*model_load(),lambda pc:0xb4000008|(((original-pc)//4)<<5),product_load,
        lambda pc:0x35000008|(((original-pc)//4)<<5),lambda pc:adrp(pc,8,0x1009db058),
        0x39000000|(0x58<<10)|(8<<5)|31,lambda pc:adrp(pc,8,0x1009d8a06),
        0x39000000|(0xa06<<10)|(8<<5)|31,lambda pc:adrp(pc,8,0x1009e3498),
        lambda pc:branch(pc,0x1001c22c4)])
    if a!=future_init:raise ValueError('Native trace init target guard')
    put(0x1001c22c0,[branch(0x1001c22c0,wrappers[0])],[0xb0004108]);entries['standard_scan_session_clear']=hex(a)
    # Owner95: the crash is a null virtual call, not an HCI/scan failure. In
    # this build's error/report path the singleton at0x1009daee0 is called via
    # vtable+0x78/+0x80 without a null check, so an absent optional reporting
    # hook branches to0. Take the build's own fall-through path instead. This
    # only omits an optional report; no HCI reply, status, capability, identity
    # or scanning state is changed or fabricated.
    if [word(a) for a in (0x1003127b0,0x1003127b4,0x1003127e0,0x1003127e4)]!=[0xf9403d08,0xd63f0100,0xaa1303e1,0xd63f0100]:
        raise ValueError('Native optional reporting-hook site guard')
    for name,site in (('optional_report_hook_0x78',0x1003127b4),('optional_report_hook_0x80',0x1003127e4)):
        put(site,[branch(site,site+4)],[0xd63f0100]);entries[name]=hex(site)
    # Native0xd7/0x2041 parameter length must equal3+5*popcount(scanning_phys),
    # matching the fields actually emitted; no byte is added or fabricated.
    put(0x100029bec,[0x11000e31],[0x11001231]);entries['standard_scan_parameter_length']=hex(0x100029bec)
    return bytes(data),dict(build='22H374',uuid=UUID,subtype='ordinary arm64',entries=entries,patches=changes,
                            private_log_sites=private_logs,optional_err_data_policy='Only exact native701/HCI Unknown Command on optional0c5b skips the unsupported feature; other statuses/original native failures and actual HCI replies retained',
                            legacy_adv_tx_power_policy='Observed Command Disallowed on legacy0x2007 uses the build-native skip branch; no TX power value or capability fabricated',
                            execution_trace_policy='At most two scalar-only init/query/model/setter lines per phase/process, four bounded exclusive-store retries; caller registers/LR/NZCV preserved; guarded writable zero-fill tail counters, no bitmap/reply/identity changes',
                            trace_counter_storage=dict(address=hex(TRACE_COUNTERS),bytes=16,initial='loader zero-fill tail after all __DATA sections',per_phase_limit=2),
                            standard_scan_encoder_policy='Native0xd7/0x2041 parameter length is3+5*popcount(scanning_phys), matching actual emitted fields; no byte added, HCI reply/status or parameter value fabricated',
                            stable_model_identity=dict(pointer=hex(MODEL_POINTER),product_offset=hex(MODEL_PRODUCT),null_model_adaptation=False,vendor_registration_unchanged=True),
                            standard_scan_policy='Stable model type0 (not volatile vendor registration) clears old scan state at native HCI initialization, then selects native standard0x2041/0x2042 only from a successful complete current0x1002 result with commands[37]&0x60==0x60; vendor registration includingffff is untouched, failed/short queries clear owned caches, stale core-config cannot overwrite the result, Apple VSC scan flags and actual HCI replies/statuses unchanged',
                            scope='physical-controller/type0 routing, private diagnostics, unsupported optional0c5b, legacy-adv-TX-power skip and real-result standard-HCI scan selection; no fabricated capabilities, firmware or keychain success',hardware_verified=False)
