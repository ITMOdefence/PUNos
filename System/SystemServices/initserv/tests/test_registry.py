import copy
import importlib.util
from pathlib import Path
import unittest
path=Path(__file__).resolve().parents[1]/'tools/generate_registry.py'
spec=importlib.util.spec_from_file_location('registry_generator',path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.registry={'native_apps':[],'services':[dict(service_id='logserv',executable='/punos/bin/logserv',arguments=['--log-path=/var/log/punos/system.log'],environment=[],enabled=True,critical=True,restart='restart',terminates_on_sigterm=True)]}
        self.metadata={'applications':[]}
    def test_valid_shared_identity(self):
        header,_,source=module.generate(self.registry,self.metadata)
        identifier=next(x for x in header.splitlines() if x.startswith('#define PLATFORM_REGISTRY_ID'))
        self.assertIn(identifier,source)
    def test_missing_logger(self):
        self.registry['services']=[]
        with self.assertRaises(ValueError):module.generate(self.registry,self.metadata)
    def test_invalid_fields(self):
        for key,values in {'enabled':[False,1],'critical':[False,1],'restart':['never','invalid'],'terminates_on_sigterm':[False,1],'executable':['','bad\0path'],'arguments':[[],['bad'],None],'environment':[['NAME'],['=value'],None]}.items():
            for value in values:
                with self.subTest(key=key,value=value):
                    registry=copy.deepcopy(self.registry);registry['services'][0][key]=value
                    with self.assertRaises(ValueError):module.generate(registry,self.metadata)
    def test_duplicate(self):
        self.registry['services']*=2
        with self.assertRaises(ValueError):module.generate(self.registry,self.metadata)
    def test_fixed_order(self):
        other=copy.deepcopy(self.registry['services'][0]);other.update(service_id='appserv',arguments=[])
        self.registry['services'].insert(0,other)
        _,_,source=module.generate(self.registry,self.metadata)
        adapter=module.load_adapter()
        entries=source.split('static const ServiceSpec services[] = {')[1]
        self.assertLess(entries.index(adapter.c_string('logserv')),entries.index(adapter.c_string('appserv')))
if __name__=='__main__':unittest.main()
