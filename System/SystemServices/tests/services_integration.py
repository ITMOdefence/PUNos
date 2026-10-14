#!/usr/bin/env python3
"""Real PUNos CRT, threads, futexes, journal and supervised child processes."""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time

HERE=Path(__file__).resolve().parents[1]
ROOT=HERE.parents[1]
MODULES={}
def module_dir(name): return MODULES.get(name,HERE/name)

def wait(check,description,timeout=15):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if check(): return
        time.sleep(.02)
    raise AssertionError('timeout: '+description)

def exact(connection,size):
    result=b''
    while len(result)<size:
        part=connection.recv(size-len(result))
        if not part: raise EOFError('peer closed')
        result+=part
    return result

def frame(kind,payload=b''): return struct.pack('<II',len(payload),kind)+payload

def recv(connection):
    size,kind=struct.unpack('<II',exact(connection,8))
    return kind,exact(connection,size)

def connect(path):
    connection=socket.socket(socket.AF_UNIX)
    connection.settimeout(15)
    connection.connect(str(path))
    return connection

def submit(component=b'test',message=b'hello',pid=123,tid=456,severity=2,version=1):
    return struct.pack('<HiiBHI',version,pid,tid,severity,len(component),len(message))+component+message

def records(path):
    if not path.exists(): return []
    data=path.read_bytes();offset=0;result=[]
    while offset<len(data):
        if len(data)-offset<27: break
        size,timestamp,pid,tid,severity,component_size,message_size=struct.unpack_from('<IQiiBHI',data,offset)
        assert size==27+component_size+message_size
        if offset+size>len(data): break
        component=data[offset+27:offset+27+component_size].decode()
        message=data[offset+27+component_size:offset+size].decode()
        result.append(dict(time=timestamp,pid=pid,tid=tid,severity=severity,component=component,message=message))
        offset+=size
    return result

def finish(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try: process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill();process.wait();raise AssertionError('SIGTERM did not terminate child')

def compile_binary(module,output,definitions,registry=None,metadata=None,extra=None,wrap=None):
    libs=['core','mem','str','file','process','time','thread','sync','eventloop']
    flags=['-std=c11','-ffreestanding','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pic','-m64','-Wall','-Wextra','-Werror','-fno-tree-loop-distribute-patterns']
    includes=[module_dir(module)/'include',module_dir(module)/'src',ROOT/'System/NativeRuntime/include',ROOT/'System/NativeRuntime/SyscallWrappers/include']
    includes += [ROOT/f'System/SystemLibs/lib{lib}/include' for lib in libs]
    includes += [ROOT/'System/SpecialLibs/ipclib/include',ROOT/'System/SpecialLibs/loglib/include',output.parent]
    flags += ['-I'+str(path) for path in includes]
    flags += [f'-D{name}="{value}"' for name,value in definitions.items()]
    sources=list((module_dir(module)/'src').glob('*.c'))
    if registry is not None:
        generated=output.parent/(module+'-generated');generated.mkdir(exist_ok=True)
        subprocess.run(['python3',str(module_dir(module)/'tools'/('generate_registry.py' if module=='initserv' else 'generate_catalog.py')),'--registry',str(registry),'--metadata',str(metadata),'--output',str(generated)],check=True)
        flags += ['-I'+str(generated)]
        sources.append(generated/('registry.c' if module=='initserv' else 'catalog.c'))
    if extra: sources.append(extra)
    objects=[]
    for index,source in enumerate(sources):
        obj=output.parent/(output.name+str(index)+'.o')
        subprocess.run(['cc',*flags,'-c',str(source),'-o',str(obj)],check=True)
        objects.append(obj)
    archives=[ROOT/f'System/SystemLibs/lib{lib}/build/libpunos_{lib}.a' for lib in libs]
    archives += [ROOT/'System/SpecialLibs/ipclib/build/libpunos_special_ipc.a',ROOT/'System/SpecialLibs/loglib/build/libpunos_special_log.a',ROOT/'System/NativeRuntime/CRT/build/libpunos_crt.a',ROOT/'System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a']
    command=['cc','-nostdlib','-no-pie','-Wl,-e,_start',str(ROOT/'System/NativeRuntime/CRT/build/crt0.o'),*map(str,objects)]
    if wrap:
        for symbol in wrap.split(','): command.append('-Wl,--wrap='+symbol)
    subprocess.run([*command,'-Wl,--start-group',*map(str,archives),'-Wl,--end-group','-o',str(output)],check=True)

def test_logserv(directory,log_binary,fault_binary,endpoint):
    log_path=directory/'journal.log'
    assert subprocess.run([str(log_binary)],timeout=15).returncode==2
    assert subprocess.run([str(log_binary),'--log-path='+str(directory/'missing/journal')],timeout=15).returncode==3
    assert not endpoint.exists()
    process=subprocess.Popen([str(log_binary),'--log-path='+str(log_path)])
    try:
        wait(endpoint.exists,'log endpoint')
        with connect(endpoint) as connection:
            checks=[(submit(),0),(submit(version=2),12),(submit(pid=0),1),(submit(tid=-1),1),(submit(severity=6),1),(submit(component=b''),1),(submit(component=b'\xff'),1),(submit(message=b'\0'),1),(submit(message=b'x'*16385),13),(submit()[:-1],1)]
            for payload,error in checks:
                connection.sendall(frame(1,payload));assert recv(connection)==(2,struct.pack('<I',error))
            connection.sendall(frame(99));assert recv(connection)==(99,struct.pack('<I',1))
            burst=b''.join(frame(1,submit(b'ordered',str(i).encode())) for i in range(180))
            connection.sendall(burst)
            for _ in range(180): assert recv(connection)==(2,struct.pack('<I',0))
            wait(lambda:len([x for x in records(log_path) if x['component']=='ordered'])==180,'ordered drain')
            client=[x for x in records(log_path) if x['component']=='ordered']
            assert [x['message'] for x in client]==list(map(str,range(180)))
            assert all(x['pid']==123 and x['tid']==456 and x['severity']==2 and x['time']>0 for x in client)
        finish(process);assert process.returncode==0 and not endpoint.exists()
        parsed=records(log_path)
        assert any(x['component']=='logserv' and x['message']=='stopping' for x in parsed)
        assert parsed[0]['component']=='logserv'
    finally: finish(process)
    # Append preserves existing journal and flush/close happens on shutdown.
    before=log_path.read_bytes()
    process=subprocess.Popen([str(log_binary),'--log-path='+str(log_path)])
    try:
        wait(endpoint.exists,'reopened endpoint');finish(process)
        assert log_path.read_bytes().startswith(before)
    finally: finish(process)
    # A real append failure is surfaced, the accepted frame is not retried.
    process=subprocess.Popen([str(fault_binary),'--log-path='+str(directory/'fault.log')])
    try:
        wait(endpoint.exists,'fault endpoint')
        with connect(endpoint) as connection:
            connection.sendall(frame(1,submit(b'fail.append')))
            kind,payload=recv(connection);assert kind==2 and struct.unpack('<I',payload)[0] in (0,8)
        assert process.wait(timeout=15)==1 and not endpoint.exists()
    finally: finish(process)
    process=subprocess.Popen([str(fault_binary),'--log-path='+str(directory/'flush.log')])
    try:
        wait(endpoint.exists,'flush fault endpoint')
        with connect(endpoint) as connection:
            connection.sendall(frame(1,submit(b'fail.flush')))
            assert recv(connection)==(2,struct.pack('<I',0))
        finish(process);assert process.returncode==1
    finally:finish(process)
    process=subprocess.Popen([str(fault_binary),'--log-path='+str(directory/'join.log')])
    try:
        wait(endpoint.exists,'join fault endpoint')
        with connect(endpoint) as connection:
            connection.sendall(frame(1,submit(b'fail.join')))
            assert recv(connection)==(2,struct.pack('<I',0))
        wait(lambda:any(x['component']=='fail.join' for x in records(directory/'join.log')),'join injection armed')
        finish(process);assert process.returncode==1
    finally:finish(process)
    # Writer is paused only in the instrumented test binary; IPC stays live.
    for byte_limit in (False,True):
        process=subprocess.Popen([str(fault_binary),'--log-path='+str(directory/('bytes.log' if byte_limit else 'count.log'))])
        try:
            wait(endpoint.exists,'pressure endpoint')
            with connect(endpoint) as connection:
                connection.sendall(frame(1,submit(b'pause')));assert recv(connection)==(2,struct.pack('<I',0))
                count=270 if byte_limit else 4120
                payload=submit(b'load',b'x'*16384 if byte_limit else b'x')
                connection.sendall(frame(1,payload)*count)
                codes=[]
                for _ in range(count):
                    kind,response=recv(connection);assert kind==2
                    codes.append(struct.unpack('<I',response)[0])
                assert 11 in codes and set(codes)<={0,11},(byte_limit,set(codes))
            finish(process);assert process.returncode==0
        finally: finish(process)
    print('logserv: protocol, ordering, persistence, worker failure, queue byte/count limits passed')

def child_pids(process):
    path=Path(f'/proc/{process.pid}/task/{process.pid}/children')
    return list(map(int,path.read_text().split())) if path.exists() else []

def find_child(process,path):
    for pid in child_pids(process):
        try:
            args=Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
            if Path(f'/proc/{pid}/exe').resolve()==path or str(path).encode() in args: return pid
        except OSError: pass
    return None

def test_initserv(directory,init_binary,log_binary,app_binary,log_endpoint,app_endpoint,registry,metadata):
    journal=directory/'supervised.log'
    process=subprocess.Popen([str(init_binary)])
    try:
        wait(lambda:log_endpoint.exists() and app_endpoint.exists(),'supervised endpoints')
        old_app=find_child(process,app_binary);old_log=find_child(process,log_binary)
        assert old_app and old_log and len(child_pids(process))==2
        with connect(app_endpoint) as connection:
            connection.sendall(frame(1));kind,payload=recv(connection)
            assert kind==2 and struct.unpack_from('<I',payload)[0]==0
        os.kill(old_app,signal.SIGTERM)
        wait(lambda:find_child(process,app_binary) not in (None,old_app),'app restart')
        assert not Path(f'/proc/{old_app}').exists()
        os.kill(old_log,signal.SIGTERM)
        wait(lambda:find_child(process,log_binary) not in (None,old_log),'log restart')
        wait(lambda:app_endpoint.exists(),'app after log restart')
        wait(lambda:any(x['component']=='initserv' and 'restarted service_id=logserv' in x['message'] for x in records(journal)),'restart diagnostic')
        wait(lambda:any(x['component']=='initserv' and 'exited service_id=logserv' in x['message'] and 'exit_value=' in x['message'] for x in records(journal)),'cached log exit status')
        live=child_pids(process);finish(process)
        assert process.returncode==0
        assert all(not Path(f'/proc/{pid}').exists() for pid in live)
        assert not log_endpoint.exists() and not app_endpoint.exists()
        wait(lambda:any(x['component']=='initserv' and x['message'].startswith('stopping') for x in records(journal)),'stopping diagnostic')
    finally: finish(process)
    # A noncritical launch fault backs off 1/2/4 seconds, with other services live.
    retry=directory/'retry'
    subprocess.run(['cc','-ffreestanding','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pic','-m64',
                    '-I'+str(ROOT/'System/NativeRuntime/SyscallWrappers/include'),'-c',str(module_dir('initserv')/'tests/child.c'),'-o',str(directory/'retry.o')],check=True)
    subprocess.run(['cc','-nostdlib','-no-pie','-Wl,-e,_start',str(ROOT/'System/NativeRuntime/CRT/build/crt0.o'),str(directory/'retry.o'),str(ROOT/'System/NativeRuntime/CRT/build/libpunos_crt.a'),str(ROOT/'System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a'),'-o',str(retry)],check=True)
    value=json.loads(registry.read_text())
    value['services'].append(dict(service_id='retry',executable=str(retry),arguments=[],environment=[],enabled=True,critical=False,restart='restart',terminates_on_sigterm=True))
    retry_registry=directory/'retry-registry.json';retry_registry.write_text(json.dumps(value))
    fault_init=directory/'fault-initserv'
    compile_binary('initserv',fault_init,{'INIT_LOG_ENDPOINT':str(log_endpoint)},retry_registry,metadata,extra=module_dir('initserv')/'tests/faults.c',wrap='process_start')
    previous=len(records(journal))
    process=subprocess.Popen([str(fault_init)])
    try:
        wait(lambda:app_endpoint.exists(),'app while noncritical service fails')
        wait(lambda:find_child(process,retry) is not None,'backoff recovery',timeout=20)
        entries=[x for x in records(journal)[previous:] if x['component']=='initserv' and 'service_id=retry' in x['message']]
        wait(lambda:len([x for x in records(journal)[previous:] if x['component']=='initserv' and 'service_id=retry' in x['message']])>=4,'retry diagnostics')
        entries=[x for x in records(journal)[previous:] if x['component']=='initserv' and 'service_id=retry' in x['message']]
        assert len(entries)>=4
        gaps=[entries[i+1]['time']-entries[i]['time'] for i in range(3)]
        assert all(value>=limit for value,limit in zip(gaps,[950,1950,3950])),gaps
        live=child_pids(process);finish(process)
        assert process.returncode==0 and all(not Path(f'/proc/{pid}').exists() for pid in live)
    finally:finish(process)
    # Logserv exec failure prevents appserv launch and returns initialization failure.
    value=json.loads(registry.read_text());value['services'][0]['executable']=str(directory/'missing-logserv')
    bad_registry=directory/'bad-registry.json';bad_registry.write_text(json.dumps(value))
    bad_init=directory/'bad-initserv'
    compile_binary('initserv',bad_init,{'INIT_LOG_ENDPOINT':str(log_endpoint)},bad_registry,metadata)
    bad=subprocess.Popen([str(bad_init)])
    try:
        assert bad.wait(timeout=15)==3
        assert not app_endpoint.exists() and child_pids(bad)==[]
    finally: finish(bad)
    print('initserv: startup ordering, service restart, logger recovery, child reaping and shutdown passed')

def main():
    global ROOT,HERE
    parser=argparse.ArgumentParser();parser.add_argument('--service',choices=['all','logserv','initserv'],default='all')
    parser.add_argument('--punos',type=Path);parser.add_argument('--source',action='append',default=[])
    args=parser.parse_args()
    if args.punos: ROOT=args.punos.resolve();HERE=ROOT/'System/SystemServices'
    for value in args.source:
        name,path=value.split('=',1);MODULES[name]=Path(path).resolve()
    with tempfile.TemporaryDirectory(prefix='base-services-') as name:
        directory=Path(name);log_endpoint=directory/'log.sock';app_endpoint=directory/'app.sock'
        log_binary=directory/'logserv';fault_binary=directory/'logserv-faults';app_binary=directory/'appserv';init_binary=directory/'initserv'
        compile_binary('logserv',log_binary,{'LOG_ENDPOINT':str(log_endpoint)})
        if args.service in ('all','logserv'):
            compile_binary('logserv',fault_binary,{'LOG_ENDPOINT':str(log_endpoint)},extra=module_dir('logserv')/'tests/faults.c',wrap='log_store_open,log_store_append,log_store_flush,log_store_close,thread_join')
            test_logserv(directory,log_binary,fault_binary,log_endpoint)
        if args.service in ('all','initserv'):
            registry=directory/'registry.json';metadata=directory/'metadata.json'
            value=json.loads((HERE/'platform_registry.json').read_text())
            value['services'][0].update(executable=str(log_binary),arguments=['--log-path='+str(directory/'supervised.log')])
            value['services'][1].update(executable=str(app_binary))
            value['services'].append(dict(service_id='disabled',executable=str(directory/'not-installed'),arguments=[],environment=[],enabled=False,critical=False,restart='never',terminates_on_sigterm=True))
            registry.write_text(json.dumps(value));metadata.write_text('{"applications":[]}')
            compile_binary('appserv',app_binary,{'APP_ENDPOINT':str(app_endpoint),'APP_LOG_ENDPOINT':str(log_endpoint)},registry,metadata)
            compile_binary('initserv',init_binary,{'INIT_LOG_ENDPOINT':str(log_endpoint)},registry,metadata)
            test_initserv(directory,init_binary,log_binary,app_binary,log_endpoint,app_endpoint,registry,metadata)
    print('base services Linux integration tests passed')
if __name__=='__main__':main()
