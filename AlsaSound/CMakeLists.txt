###########################################################################
# Copyright 2015, 2016, 2017 IoT.bzh
#
# author: Fulup Ar Foll <fulup@iot.bzh>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###########################################################################

PROJECT(alsa-bindings C)
PKG_CHECK_MODULES(alsa REQUIRED alsa)

# Max Sound Card Number eligible to ctlevent subscription
defstr(MAX_SND_CARD  16)

SET(link_libraries
	${alsa_LIBRARIES}
        ${link_libraries}
	)

ADD_SUBDIRECTORY(CoreBinding)
ADD_SUBDIRECTORY(HALayer)

