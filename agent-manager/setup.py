# MIT License
#
# Copyright (c) 2019-2021 Ecole Polytechnique Federale Lausanne (EPFL)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
import logging
import os
import shutil
logging.basicConfig()
log = logging.getLogger(__file__)
log.setLevel(logging.DEBUG)

try:
    import pathlib
except ImportError:
    log.error("Must use Python 3!")
    raise

from setuptools import setup, find_packages
manager_path = (pathlib.Path(__file__).parent / "manager").absolute()
assert manager_path.exists() and manager_path.is_dir(), "manager path is missing at {}".format(manager_path.absolute())
cmake_build_path = pathlib.Path("../agents/assets").absolute()

shutil.rmtree("manager/assets", ignore_errors=True)
os.mkdir("manager/assets")
target = cmake_build_path / "agents/agent"
shutil.copyfile(target.absolute().as_posix(), "manager/assets/agent")
shutil.copymode(target.absolute().as_posix(), "manager/assets/agent")
target = cmake_build_path / "agents/librand.so"
shutil.copyfile(target.absolute().as_posix(), "manager/assets/librand.so")
shutil.copymode(target.absolute().as_posix(), "manager/assets/librand.so")

if not cmake_build_path.exists():
    raise Exception("binary assets folder needs to be built using the CMake build system. See the README")

setup(
    name="lancet-manager",
    version="0.1.0",
    url="https://github.com/epfl-dcsl/lancet-tool",
    author="Marios Kogias",
    author_email="marios.kogias@epfl.ch",
    packages=find_packages(),
    zip_safe=False,
    install_requires=[
        "scipy",
        "posix_ipc",
        "statsmodels"
    ],
    package_data={'manager': ["assets/agent",
                              "assets/librand.so"]},
    include_package_data=True,
    entry_points = {
        'console_scripts': ['lancet=manager.lancet:main']
    },
    python_requires=">=3", # TODO not totally sure about the language requirements here
    setup_requires=['wheel'],
)
