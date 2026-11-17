#!/usr/bin/env python3
"""Actual bridge formatter and C buffer copying against typed runtime fixtures."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rustc',required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
s=(Path(__file__).resolve().parents[1]/'drivers/PongMetal/bridge/NativeMetalObjects.rs').read_text()
def function(signature):
 start=s.index(signature);brace=s.index('{',start);depth=0
 for end in range(brace,len(s)):
  depth+=(s[end]=='{')-(s[end]=='}')
  if not depth:return s[start:end+1]
formatter=function('fn command_buffer_error_text(');copy=function('fn copy_out(')
fixture=r'''
use std::ffi::c_char;
use std::cell::Cell;
mod metal2vulkan_runtime {
 #[derive(Clone,Copy,Debug)]
 pub enum CommandBufferError {DeviceLost,Refused,CallRefused(()),PipelineCreation(())}
}
use metal2vulkan_runtime::CommandBufferError;
struct FailedPipeline {encoder:usize,draw:usize,error:&'static str}
struct CommandBuffer {error:Option<CommandBufferError>,description:Option<String>,details:Cell<unsigned>}
type unsigned=u32;
impl CommandBuffer {
 fn error(&self)->Option<CommandBufferError>{self.error}
 fn error_description(&self)->Option<String>{self.description.clone()}
 fn refusal(&self)->Option<&'static str>{self.details.set(self.details.get()+1);Some("Invalid(AttachmentFormat(Color(0)))")}
 fn failed_pipeline(&self)->Option<FailedPipeline>{self.details.set(self.details.get()+1);Some(FailedPipeline{encoder:2,draw:7,error:"OutOfMemory(Device)"})}
}
'''.replace('Cell<unsigned>','Cell<u32>').replace('type unsigned=u32;','')
checks=r'''
fn main() {
 let mut cb=CommandBuffer{error:None,description:None,details:Cell::new(0)};
 assert!(command_buffer_error_text(&cb).is_none());assert_eq!(cb.details.get(),0);
 cb.error=Some(CommandBufferError::DeviceLost);
 assert_eq!(command_buffer_error_text(&cb).unwrap(),"DeviceLost");assert_eq!(cb.details.get(),0);
 cb.description=Some("driver context lost".into());assert!(command_buffer_error_text(&cb).unwrap().contains("DeviceLost: driver context lost"));
 cb.description=None;
 for error in [CommandBufferError::Refused,CommandBufferError::CallRefused(())] {
  cb.error=Some(error);assert!(command_buffer_error_text(&cb).unwrap().contains("refusal="));
 }
 cb.error=Some(CommandBufferError::PipelineCreation(()));
 let text=command_buffer_error_text(&cb).unwrap();assert!(text.contains("encoder=2 draw=7"));assert!(text.contains("OutOfMemory(Device)"));
 let mut bytes=[0x55u8;24];
 assert_eq!(copy_out(&text,std::ptr::null_mut(),0),text.len());
 assert_eq!(copy_out(&text,bytes[1..].as_mut_ptr().cast(),8),text.len());
 assert_eq!(&bytes[1..8],&text.as_bytes()[..7]);assert_eq!(bytes[8],0);assert_eq!(bytes[0],0x55);assert_eq!(bytes[9],0x55);
 copy_out(&text,bytes[1..].as_mut_ptr().cast(),1);assert_eq!(bytes[1],0);
 println!("PASS actual bridge error text: DeviceLost, refusal, pipeline cause, relayed detail, nonfailed query and bounded/NUL C output");
}
'''
with tempfile.TemporaryDirectory(prefix='metal-error-',dir=os.environ.get('TMPDIR')) as td:
 td=Path(td);source=td/'test.rs';binary=td/'test';source.write_text(fixture+formatter+'\n'+copy+'\n'+checks)
 r=subprocess.run([a.rustc,'--edition=2024','-Dwarnings',str(source),'-o',str(binary)],text=True,capture_output=True,timeout=60)
 (a.output_dir/'compile.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stdout+r.stderr)
 r=subprocess.run([binary],text=True,capture_output=True,timeout=15);(a.output_dir/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
 if r.returncode:raise SystemExit(r.returncode)
