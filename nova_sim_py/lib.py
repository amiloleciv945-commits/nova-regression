import os
from pathlib import Path
from ctypes import CDLL, Structure, c_char_p, c_int


def load_library():
    def suffix() -> str:
        return ".dll" if os.name == "nt" else ".so"

    bin_folder = str((Path(__file__).parent / 'binaries').resolve())
    lib_name = f"libnovac{suffix()}"

    current_path = os.environ.get("PATH", "")

    # Add the executable directory to the PATH if not already present
    if bin_folder not in current_path:
        os.environ["PATH"] = bin_folder + os.pathsep + current_path

    if os.name == "nt":
        with os.add_dll_directory(bin_folder):
            return CDLL(lib_name)
    else:
        return CDLL(f"{bin_folder}/{lib_name}")


dll = load_library()


class Version(Structure):
    _fields_ = [("major", c_int), ("minor", c_int), ("patch", c_int)]

    def __repr__(self):
        return f"v{self.major}.{self.minor}.{self.patch}"


class NovaLib:

    @staticmethod
    def version():
        version_fun = dll.nova_library_version
        version_fun.restype = Version
        return dll.nova_library_version()

    @staticmethod
    def get_last_error():
        get_last_error_msg = dll.nova_last_error_msg
        get_last_error_msg.restype = c_char_p
        err = get_last_error_msg()
        return err.decode()

    @staticmethod
    def set_log_level(lvl: str):
        set_log_level = dll.nova_set_log_level
        set_log_level.argtypes = [c_char_p]
        set_log_level(lvl.encode())

