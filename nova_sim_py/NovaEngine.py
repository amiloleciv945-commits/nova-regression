from .lib import dll, NovaLib
from .NovaSimulationStructure import NovaSimulationStructure
from ctypes import (
    CFUNCTYPE, POINTER, Structure, byref, c_bool, c_char_p, c_double, c_int,
    c_size_t, c_uint, c_void_p, create_string_buffer,
)
from math import isnan


class _AlgorithmConfig(Structure):
    _fields_ = [
        ("type", c_int),
        ("parallel", c_bool),
        ("order", POINTER(c_char_p)),
        ("order_size", c_size_t),
        ("max_iterations", c_size_t),
        ("output_holder_type", c_int),
        ("input_holder_type", c_int),
    ]


def _algorithm_config(algorithm):
    if isinstance(algorithm, str):
        algorithm = {"type": algorithm}
    if not isinstance(algorithm, dict):
        raise TypeError("algorithm must be a name or a configuration dictionary")

    algorithms = {
        "fixed_step": (0, {"parallel"}),
        "gauss_seidel": (1, {"order"}),
        "strong_coupling": (2, {"parallel", "max_iterations", "output_holder_type", "input_holder_type"}),
    }
    kind = algorithm.get("type")
    if not isinstance(kind, str) or kind not in algorithms:
        raise ValueError("algorithm type must be fixed_step, gauss_seidel, or strong_coupling")
    type_id, options = algorithms[kind]
    unknown = algorithm.keys() - options - {"type"}
    if unknown:
        names = ", ".join(sorted(map(repr, unknown)))
        raise ValueError(f"Unsupported options for {kind}: {names}")

    config = _AlgorithmConfig(type=type_id)
    if kind == "gauss_seidel":
        order = algorithm.get("order")
        if not isinstance(order, (list, tuple)) or not order or any(
            not isinstance(name, str) or not name or "\0" in name for name in order
        ):
            raise ValueError("Gauss-Seidel order must be a nonempty list or tuple of FMU instance names")
        config.order = (c_char_p * len(order))(*(name.encode() for name in order))
        config.order_size = len(order)
        return config

    parallel = algorithm.get("parallel", kind == "fixed_step")
    if not isinstance(parallel, bool):
        raise TypeError("algorithm parallel must be a boolean")
    config.parallel = parallel

    if kind == "strong_coupling":
        iterations = algorithm.get("max_iterations", 1)
        if type(iterations) is not int or not 0 <= iterations <= c_size_t(-1).value:
            raise ValueError("max_iterations must fit in an unsigned size_t")
        config.max_iterations = iterations
        for name in ("output_holder_type", "input_holder_type"):
            value = algorithm.get(name, 0)
            if type(value) is not int or not 0 <= value <= 3:
                raise ValueError(f"{name} must be an integer from 0 to 3")
            setattr(config, name, value)
    return config


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

    def __init__(self, step_size: float, ssp_path: str = None, structure: NovaSimulationStructure = None,
                 *, algorithm: str | dict | None = None):
        """Create an engine. The default algorithm is parallel fixed step."""
        self.sim = None
        if ssp_path is not None and structure is not None:
            raise ValueError("Only one of 'ssp_path' or 'structure' should be provided.")
        if ssp_path is None and structure is None:
            raise ValueError("Either 'ssp_path' or 'structure' must be provided.")

        self.listener_configs = {}
        # The native engine keeps copies of the connections after the structure is freed.
        self._connection_modifiers = tuple(structure.modifiers) if structure is not None else ()
        self._supports_algorithm_options = hasattr(dll, "nova_engine_create_with_algorithm")

        self._init_simulation = dll.nova_engine_init
        self._init_simulation.argtypes = [c_void_p, c_double, c_char_p]
        self._init_simulation.restype = c_bool

        self._step_simulation = dll.nova_engine_step
        self._step_simulation.argtypes = [c_void_p, c_uint]
        self._step_simulation.restype = c_double

        self._step_simulation_for = dll.nova_engine_step_for
        self._step_simulation_for.argtypes = [c_void_p, c_double]
        self._step_simulation_for.restype = None

        self._step_simulation_until = dll.nova_engine_step_until
        self._step_simulation_until.argtypes = [c_void_p, c_double]
        self._step_simulation_until.restype = None

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
            create_name, source_type, source = "nova_engine_create_from_ssp", c_char_p, ssp_path.encode()
        else:
            create_name, source_type, source = "nova_engine_create", c_void_p, structure.handle

        argtypes = [source_type, c_double]
        args = [source, step_size]
        if algorithm is not None and algorithm != "fixed_step":
            config = _algorithm_config(algorithm)
            create_name += "_with_algorithm"
            argtypes.append(POINTER(_AlgorithmConfig))
            args.append(byref(config))
        create_engine = getattr(dll, create_name, None)
        if create_engine is None:
            raise RuntimeError("This libnovac does not support algorithm options. Rebuild or update the native library.")
        create_engine.argtypes = argtypes
        create_engine.restype = c_void_p
        self.sim = create_engine(*args)
        if self.sim is None:
            raise RuntimeError(NovaLib.get_last_error())

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
        time = self._step_simulation(self.sim, num_steps)
        if isnan(time):
            raise RuntimeError(NovaLib.get_last_error() or "Nova step failed")
        return time

    def step_for(self, duration: float):
        self._step_simulation_for(self.sim, duration)
        self._raise_step_error()

    def step_until(self, time_point: float):
        self._step_simulation_until(self.sim, time_point)
        self._raise_step_error()

    def _raise_step_error(self):
        if self._supports_algorithm_options:
            error = NovaLib.get_last_error()
            if error:
                raise RuntimeError(error)

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
            self._connection_modifiers = ()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.free()

    def __del__(self):
        self.free()



