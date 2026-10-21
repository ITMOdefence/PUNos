#!/usr/bin/env python3
"""Generate initserv and the shared appserv catalogue from one registry."""
import argparse
import importlib.util
from pathlib import Path

def load_adapter():
    path = Path(__file__).resolve().parent / 'generate_catalog.py'
    if not path.exists():
        path = Path(__file__).resolve().parents[2] / 'appserv/tools/generate_catalog.py'
    spec = importlib.util.spec_from_file_location('catalog_adapter', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def generate(registry, metadata):
    adapter = load_adapter()
    header, catalogue = adapter.generate(registry, metadata)
    services = registry.get('services')
    if not isinstance(services, list) or not services or len(services) > 64:
        raise ValueError('registry requires 1..64 ServiceSpecs')
    fields = {'service_id','executable','arguments','environment','enabled','critical','restart','terminates_on_sigterm'}
    def digits(value):
        return bool(value) and value.isascii() and value.isdecimal()
    seen = set()
    order = {'logserv':0,'appserv':1,'IOserv':2,'locationserv':3,'camserv':4}
    validated = []
    for service in services:
        if not isinstance(service, dict) or set(service) != fields:
            raise ValueError('ServiceSpec fields do not match schema')
        name = adapter.text(service['service_id'], 'service_id', 64, True)
        if name in seen: raise ValueError('duplicate service_id')
        seen.add(name)
        adapter.text(service['executable'], 'executable', nonempty=True)
        if service['terminates_on_sigterm'] is not True: raise ValueError('SIGTERM contract required')
        if any(type(service[key]) is not bool for key in ('enabled','critical')):
            raise ValueError('enabled/critical must be booleans')
        if service['restart'] not in ('restart','never'): raise ValueError('invalid restart policy')
        for field in ('arguments','environment'):
            if not isinstance(service[field],list): raise ValueError('expected string vector')
            for value in service[field]:
                adapter.text(value,field)
                if field=='environment' and ('=' not in value or value.startswith('=')):
                    raise ValueError('environment requires NAME=value')
        if name == 'IOserv':
            if not service['executable'].endswith('/IOserv'):
                raise ValueError('IOserv executable name must be IOserv')
            if service['enabled'] and (not service['arguments'] or any(
                not value.startswith('--device=/dev/input/event') or
                not digits(value[len('--device=/dev/input/event'):])
                for value in service['arguments'])):
                raise ValueError('enabled IOserv requires explicit --device=/dev/input/eventN')
        if name == 'camserv':
            if not service['executable'].endswith('/camserv'):
                raise ValueError('camserv executable name must be camserv')
            if service['enabled']:
                args = service['arguments']
                parts = [value.split('=', 1) for value in args]
                values = {part[0]: part[1] for part in parts if len(part) == 2}
                if len(args) != 5 or len(values) != 5 or set(values) != {
                    '--device','--format','--width','--height','--fps'} or \
                    not values['--device'].startswith('/dev/video') or \
                    not digits(values['--device'][len('/dev/video'):]) or \
                    values['--format'] not in ('Yuyv','Mjpeg','Nv12') or any(
                        not digits(values[key]) or int(values[key]) <= 0 or
                        int(values[key]) > 0xffffffff
                        for key in ('--width','--height','--fps')):
                    raise ValueError('enabled camserv requires explicit device, format, width, height and fps')
        validated.append(service)
    validated.sort(key=lambda item: order.get(item['service_id'],5))
    log = validated[0]
    if log['service_id']!='logserv' or not log['enabled'] or not log['critical'] or log['restart']!='restart':
        raise ValueError('logserv must be enabled, critical and restartable')
    if len(log['arguments'])!=1 or not log['arguments'][0].startswith('--log-path=') or len(log['arguments'][0])<=11:
        raise ValueError('logserv requires one --log-path argument')
    lines=['#include <initserv.h>', next(line for line in header.splitlines() if line.startswith('#define PLATFORM_REGISTRY_ID '))]
    for i,service in enumerate(validated):
        lines.append(f'static const String executable_{i} = '+adapter.string_initializer(service['executable'])+';')
        for field in ('arguments','environment'):
            if service[field]:
                lines.append(f'static const String {field}_{i}[] = '+'{'+','.join(adapter.string_initializer(x) for x in service[field])+'};')
    lines.append('static const ServiceSpec services[] = {')
    for i,service in enumerate(validated):
        launch=[f'&executable_{i}']
        for field in ('arguments','environment'):
            launch += [f'{field}_{i}' if service[field] else 'NULL',str(len(service[field]))]
        flags=[str(service['enabled']).lower(),str(service['critical']).lower(),str(service['restart']=='restart').lower()]
        lines.append('{'+adapter.string_initializer(service['service_id'])+', {'+','.join(launch)+'}, '+','.join(flags)+'},')
    lines += ['};','const PlatformRegistry init_registry = { { (char *)PLATFORM_REGISTRY_ID,64 }, services, sizeof(services)/sizeof(services[0]) };','']
    return header,catalogue,'\n'.join(lines)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('registry','metadata','output'): parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    adapter=load_adapter()
    try:
        header,catalogue,source=generate(adapter.read_json(args.registry),adapter.read_json(args.metadata))
    except (ValueError,UnicodeError,OSError) as error: parser.exit(1,str(error)+'\n')
    output=Path(args.output); output.mkdir(parents=True,exist_ok=True)
    (output/'platform_registry.h').write_text(header)
    (output/'catalog.c').write_text(catalogue)
    (output/'registry.c').write_text(source)
if __name__=='__main__': main()
