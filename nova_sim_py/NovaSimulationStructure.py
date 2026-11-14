import os
from .NovaParameterSet import NovaParameterSet
from .lib import dll, NovaLib
from ctypes import c_void_p, c_bool, c_char_p, CFUNCTYPE, c_double

class NovaSimulationStructure:

    def __init__(self):

        self.modifiers = [] # to keep modifier function alive

        create_simulation_structure = dll.nova_simulation_structure_create
        create_simulation_structure.restype = c_void_p

        self._add_model = dll.nova_simulation_structure_add_model
        self._add_model.argtypes = [c_void_p, c_char_p, c_char_p]
        self._add_model.restype = c_bool

        self._add_parameter_set = dll.nova_simulation_structure_add_parameter_set
        self._add_parameter_set.argtypes = [c_void_p, c_char_p, c_void_p]
        self._add_parameter_set.restype = c_bool

        self._make_int_connection = dll.nova_simulation_structure_make_int_connection
        self._make_int_connection.argtypes = [c_void_p, c_char_p, c_char_p]

        self._make_real_connection_mod = dll.nova_simulation_structure_make_real_connection_mod
        self._make_real_connection_mod.argtypes = [c_void_p, c_char_p, c_char_p, CFUNCTYPE(c_double, c_double)]

        self._make_real_connection = dll.nova_simulation_structure_make_real_connection
        self._make_real_connection.argtypes = [c_void_p, c_char_p, c_char_p]

        self._make_string_connection = dll.nova_simulation_structure_make_string_connection
        self._make_string_connection.argtypes = [c_void_p, c_char_p, c_char_p]

        self._make_bool_connection = dll.nova_simulation_structure_make_bool_connection
        self._make_bool_connection.argtypes = [c_void_p, c_char_p, c_char_p]

        self._make_vector_connection = dll.nova_simulation_structure_make_vector_connection
        self._make_vector_connection.argtypes = [c_void_p, c_char_p, c_char_p]


        self._handle = create_simulation_structure()
        if self._handle is None:
            raise Exception(NovaLib.get_last_error())

    def add_model(self, instance_name: str, uri: str | os.PathLike):
        if isinstance(uri, os.PathLike):
            uri = os.fspath(uri)
        if not self._add_model(self.handle, instance_name.encode(), uri.encode()):
            raise Exception(NovaLib.get_last_error())

    def add_parameter_set(self, name: str, parameters: NovaParameterSet | dict[str, any]):
        if isinstance(parameters, NovaParameterSet):
            return self._add_parameter_set(self.handle, name.encode(), parameters.handle)
        elif isinstance(parameters, dict):
            with (NovaParameterSet()) as params:
                for key, value in parameters.items():
                    if isinstance(value, bool):
                        params.add_bool(key, value)
                    elif isinstance(value, float):
                        params.add_real(key, value)
                    elif isinstance(value, int):
                        params.add_int(key, value)
                    elif isinstance(value, str):
                        params.add_string(key, value)
                    else:
                        raise Exception("Illegal value type. Must be int, float, bool or str")
                return self.add_parameter_set(name, params)

        else:
            raise Exception("Illegal parameter type. Must be NovaParameterSet or dict")


    def make_real_connection(self, source: str, sink: str, modifier = None):

        if modifier is not None:
            @CFUNCTYPE(c_double, c_double)
            def _modifier(value: float) -> float:
                return modifier(value)

            self.modifiers.append(_modifier)
            self._make_real_connection_mod(self.handle, source.encode(), sink.encode(), _modifier)

        else:
            self._make_real_connection(self.handle, source.encode(), sink.encode())

    def make_int_connection(self, source: str, sink: str):
        self._make_int_connection(self.handle, source.encode(), sink.encode())

    def make_string_connection(self, source: str, sink: str):
        self._make_string_connection(self.handle, source.encode(), sink.encode())

    def make_bool_connection(self, source: str, sink: str):
        self._make_bool_connection(self.handle, source.encode(), sink.encode())
    
    def make_vector_connection(self, source: str, sink: str):
        self._make_vector_connection(self.handle, source.encode(), sink.encode())

    @property
    def handle(self):
        return self._handle

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.free()

    def free(self):
        if not self._handle is None:
            simulation_structure_destroy = dll.nova_simulation_structure_destroy
            simulation_structure_destroy.argtypes = [c_void_p]

            simulation_structure_destroy(self._handle)
            self._handle = None
            self.modifiers.clear()

    def __del__(self):
        self.free()

