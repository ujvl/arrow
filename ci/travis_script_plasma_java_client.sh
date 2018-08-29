#!/usr/bin/env bash

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

set -e

PLASMA_JAVA_DIR=${TRAVIS_BUILD_DIR}/java/plasma

pushd $PLASMA_JAVA_DIR

mvn clean install
export PLASMA_STORE=${TRAVIS_BUILD_DIR}/cpp-install/bin/plasma_store_server
java -cp target/test-classes:target/classes -Djava.library.path=${TRAVIS_BUILD_DIR}/cpp-build/debug/ org.apache.arrow.plasma.PlasmaClientTest

popd
