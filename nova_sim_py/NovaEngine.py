from .lib import dll, NovaLib
from .NovaSimulationStructure import NovaSimulationStructure
from ctypes import create_string_buffer, c_size_t, c_bool, c_int, c_void_p, c_double, c_char_p, c_size_t, POINTER, Structure, CFUNCTYPE, byref, c_char


class SimulationInfo(Structure):
    _fields_ = [("time", c_double), ("iterations", c_size_t)]


class SimulationListener:

    def pre(self, info: SimulationInfo):
        pass

    def post(self, info: SimulationInfo):
        pass


class ListenerConfig(Structure):
    _fields_ = [
        ("preStepCallback", CFUNCTYPE(None, SimulationInfo)),
        ("postStepCallback", CFUNCTYPE(None, SimulationInfo))
    ]


class NovaEngine:

    def __init__(self, step_size: float, ssp_path: str = None, structure: NovaSimulationStructure = None):

        if ssp_path is not None and structure is not None:
            raise ValueError("Only one of 'ssp_path' or 'structure' should be provided.")

        self.listener_configs = {}

        self._init_simulation = dll.nova_engine_init
        self._init_simulation.argtypes = [c_void_p, c_double, c_char_p]
        self._init_simulation.restype = c_bool

        self._step_simulation = dll.nova_engine_step
        self._step_simulation.argtypes = [c_void_p, c_size_t]
        self._step_simulation.restype = c_double

        self._step_simulation_for = dll.nova_engine_step_for
        self._step_simulation_for.argtypes = [c_void_p, c_double]

        self._step_simulation_until = dll.nova_engine_step_until
        self._step_simulation_until.argtypes = [c_void_p, c_double]

        self._get_integer = dll.nova_engine_get_integer
        self._get_integer.argtypes = [c_void_p, c_char_p, c_char_p, POINTER(c_int)]
        self._get_integer.restype = c_bool

        self._get_real = dll.nova_engine_get_real
        self._get_real.argtypes = [c_void_p, c_char_p, c_char_p, POINTER(c_double)]
        self._get_real.restype = c_bool

        self._get_bool = dll.nova_engine_get_bool
        self._get_bool.argtypes = [c_void_p, c_char_p, c_char_p, POINTER(c_bool)]
        self._get_bool.restype = c_bool

        self._get_string = dll.nova_engine_get_string
        self._get_string.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p, c_size_t]
        self._get_string.restype = c_bool

        self._set_integer = dll.nova_engine_set_integer
        self._set_integer.argtypes = [c_void_p, c_char_p, c_char_p, c_int]
        self._set_integer.restype = c_bool

        self._set_real = dll.nova_engine_set_real
        self._set_real.argtypes = [c_void_p, c_char_p, c_char_p, c_double]
        self._set_real.restype = c_bool

        self._set_bool = dll.nova_engine_set_bool
        self._set_bool.argtypes = [c_void_p, c_char_p, c_char_p, c_bool]
        self._set_bool.restype = c_bool

        self._set_string = dll.nova_engine_set_string
        self._set_string.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p]
        self._set_string.restype = c_bool

        self._create_listener = dll.nova_engine_listener_create
        self._create_listener.argtypes = [ListenerConfig]
        self._create_listener.restype = c_void_p

        self._create_csv_writer = dll.nova_csv_writer_create
        self._create_csv_writer.argtypes = [c_char_p, c_char_p]
        self._create_csv_writer.restype = c_void_p

        self._add_listener = dll.nova_engine_add_listener
        self._add_listener.argtypes = [c_void_p, c_char_p, c_void_p]

        self._remove_listener = dll.nova_engine_remove_listener
        self._remove_listener.argtypes = [c_void_p, c_char_p]

        if ssp_path is not None:

            _create_simulation_from_ssp = dll.nova_engine_create_from_ssp
            _create_simulation_from_ssp.argtypes = [c_char_p, c_double]
            _create_simulation_from_ssp.restype = c_void_p

            self.sim = _create_simulation_from_ssp(ssp_path.encode(), step_size)
            if self.sim is None:
                raise Exception(NovaLib.get_last_error())

        elif structure is not None:
            _create_simulation_from_structure = dll.nova_engine_create
            _create_simulation_from_structure.argtypes = [c_void_p, c_double]
            _create_simulation_from_structure.restype = c_void_p

            self.sim = _create_simulation_from_structure(structure.handle, step_size)
            if self.sim is None:
                raise Exception(NovaLib.get_last_error())

        else:
            raise ValueError("Either 'ssp_path' or 'structure' must be provided.")

    def add_listener(self, name: str, listener: SimulationListener):

        if not isinstance(listener, SimulationListener):
            raise Exception("listener must be of type SimulationListener")

        listener.name = name

        config = ListenerConfig()
        self.listener_configs[name] = config

        @CFUNCTYPE(None, SimulationInfo)
        def pre(info: SimulationInfo):
            listener.pre(info)

        config.preStepCallback = pre

        @CFUNCTYPE(None, SimulationInfo)
        def post(info: SimulationInfo):
            listener.post(info)

        config.postStepCallback = post

        cpp_listener = self._create_listener(config)
        self._add_listener(self.sim, name.encode(), cpp_listener)

    def remove_listener(self, name: str):
        self._remove_listener(self.sim, name.encode())

    def add_csv_writer(self, result_file: str, csv_config: str = None, decimationFactor: int = None):

        listener = self._create_csv_writer(result_file.encode(),
                                           None if csv_config is None else csv_config.encode())
        if listener is None:
            raise Exception(NovaLib.get_last_error())

        if decimationFactor is not None:
            set_decimation = dll.nova_csv_writer_set_decimation_factor
            set_decimation.argtypes = [c_void_p, c_int]
            set_decimation.restype = c_bool

            if not set_decimation(listener, decimationFactor):
                raise Exception(NovaLib.get_last_error())

        self._add_listener(self.sim, b'csv_writer', listener)

    def load_scenario(self, scenario_file: str):
        _load_scenario = dll.nova_engine_load_scenario
        _load_scenario.argtypes = [c_void_p, c_char_p]
        _load_scenario.restype = c_bool

        _load_scenario(self.sim, scenario_file.encode())


    def get_integer(self, instance: str, variable: str):
        val = c_int()
        if not self._get_integer(self.sim, instance.encode(), variable.encode(), byref(val)):
            raise Exception(NovaLib.get_last_error())
        return val.value

    def get_real(self, instance: str, variable: str):
        val = c_double()
        if not self._get_real(self.sim, instance.encode(), variable.encode(), byref(val)):
            raise Exception(NovaLib.get_last_error())
        return val.value

    def get_bool(self, instance: str, variable: str):
        val = c_bool()
        if not self._get_bool(self.sim, instance.encode(), variable.encode(), byref(val)):
            raise Exception(NovaLib.get_last_error())
        return val.value

    def get_string(self, instance: str, variable: str):
        val = create_string_buffer(1024)
        if not self._get_string(self.sim, instance.encode(), variable.encode(), val, 1024):
            raise Exception(NovaLib.get_last_error())
        return val.value.decode('utf-8')

    def set_integer(self, instance: str, variable: str, value: int):
        if not self._set_integer(self.sim, instance.encode(), variable.encode(), value):
            raise Exception(NovaLib.get_last_error())

    def set_real(self, instance: str, variable: str, value: float):
        if not self._set_real(self.sim, instance.encode(), variable.encode(), value):
            raise Exception(NovaLib.get_last_error())

    def set_bool(self, instance: str, variable: str, value: bool):
        if not self._set_bool(self.sim, instance.encode(), variable.encode(), value):
            raise Exception(NovaLib.get_last_error())

    def set_string(self, instance: str, variable: str, value: str):
        if not self._set_string(self.sim, instance.encode(), variable.encode(), value.encode()):
            raise Exception(NovaLib.get_last_error())

    def init(self, start_time: int = 0, parameter_set: str = None):
        return self._init_simulation(self.sim, start_time, None if parameter_set is None else parameter_set.encode())

    def step(self, num_steps: int = 1) -> float:
        return self._step_simulation(self.sim, num_steps)

    def step_for(self, duration: float):
        self._step_simulation_until(self.sim, duration)

    def step_until(self, time_point: float):
        self._step_simulation_until(self.sim, time_point)

    def reset(self) -> bool:
        reset_simulation = dll.nova_engine_reset
        reset_simulation.argtypes = [c_void_p]
        reset_simulation.restype = c_bool

        return reset_simulation(self.sim)

    def terminate(self) -> bool:
        terminate_simulation = dll.nova_engine_terminate
        terminate_simulation.argtypes = [c_void_p]
        terminate_simulation.restype = c_bool

        return terminate_simulation(self.sim)

    def free(self):
        if not self.sim is None:
            destroy_simulation = dll.nova_engine_destroy
            destroy_simulation.argtypes = [c_void_p]

            destroy_simulation(self.sim)
            self.sim = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.free()

    def __del__(self):
        self.free()



