#!/bin/bash

# Parameters
LIBS_PATH="/home/igor/Desktop/OLR/libs"
OLR_PATH="/home/igor/Desktop/OLR/"

OLR_VERSION="1.0.2"
ORACLE_MAJOR="19"
ORACLE_MINOR="24"
PROTOBUF_VERSION="3.21.12"
RAPIDJSON_VERSION="1.1.0"

USAGE="$0 [--help|--version]\n"`
`"$0 install [flags] \n"`
`"flags:\n"`
`"\t --packages \t - Установка пакетов и инструментов для установки и сборки OpenLogReplicator. \n"`
`"\t --oracle \t - Установка Oracle API. \n"`
`"\t --rapidjson \t - RapidJson библиотека, обязательная для сборки. \n"`
`"\t --protobuf \t - Установка и компиляция библиотеки protobuf. \n"`
`"\t --replicator \t - Установка исходников OpenLogReplicator. \n"`
`"\t --all \t\t - Объединение всех флагов. \n"`
`"\t --minimum \t - Установка минимального количества нужных компонент (OLR, RapidJson и пакеты). \n"`
`"$0 build [mode] [lib <path>] \n"`
`"mode:\n"`
`"\t --debug \t - Отладочная сборка (по умолчанию). \n"`
`"\t --release \t - Релизная сборка. \n"`
`"lib:\n"`
`"\t --oracle \t - Путь до Oracle API (по умолчанию: LIBS_PATH/instantclient_ORACLE_MINOR_ORACLE_MAJOR). \n"`
`"\t --rapidjson \t - Путь до RapidJson (по умолчанию: LIBS_PATH/rapidjson). \n"`
`"\t --protobuf \t - Путь до Oracle API (по умолчанию: LIBS_PATH/protobuf). \n"`
`"\t --no-protobuf \t - Сборка без protobuf. \n"`
`"\t --no-oracle \t - Сборка без Oracle API. \n"

CENTOS_PACKS="autoconf automake diffutils file gcc gcc-c++ libaio libaio-devel libarchive libasan libnsl libtool make patch tar unzip wget zlib-devel git"
DEBIAN_PACKS="file gcc g++ libaio1 libasan6 libtool libz-dev make patch unzip wget cmake git"

check() {
    if test $1 -ne 0; then 
        exit $1;
    fi

    return 0; 
}

install_packages() {
    echo "Install tool packages: ";

    if [ -r /etc/centos-release ]; then
        sudo yum -y install ${CENTOS_PACKS};
        check $?;
    fi

    if [ -r /etc/oracle-release ]; then
        sudo yum -y install ${CENTOS_PACKS};
        check $?;
    fi

    if [ -r /etc/debian_version ]; then
        sudo apt-get update ;
        check $?;
        sudo apt-get -y install ${DEBIAN_PACKS};
        check $?;
    fi
}

install_rapidjson() {
    echo "Install RapidJson ver: ${RAPIDJSON_VERSION} into ${LIBS_PATH}/rapidjson";

    if [ -d ${LIBS_PATH} ]; then
        rm -rf ${LIBS_PATH}/rapidjson*;
        check $?;
    else 
        mkdir --parents --mode 777 ${LIBS_PATH};
        check $?;
    fi

    cd "${LIBS_PATH}";
    check $?;
    wget -nv https://github.com/Tencent/rapidjson/archive/refs/tags/v${RAPIDJSON_VERSION}.tar.gz;
    check $?;
    tar xzf v${RAPIDJSON_VERSION}.tar.gz;
    check $?;
    rm v${RAPIDJSON_VERSION}.tar.gz;
    check $?;
    mv rapidjson-${RAPIDJSON_VERSION} rapidjson;
    check $?;
}

install_oracle() {
    echo "Install Oracle API ver: ${ORACLE_MAJOR}.${ORACLE_MINOR} into ${LIBS_PATH}/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR}";

    if [ -d ${LIBS_PATH} ]; then
        rm -rf ${LIBS_PATH}/instantclient*;
        check $?;
    else 
        mkdir --parents --mode 777 ${LIBS_PATH};
        check $?;
    fi

    cd "${LIBS_PATH}";
    check $?;
    wget -nv https://download.oracle.com/otn_software/linux/instantclient/${ORACLE_MAJOR}${ORACLE_MINOR}000/instantclient-basic-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0dbru.zip;
    check $?;
    unzip instantclient-basic-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0dbru.zip;
    check $?;
    rm instantclient-basic-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0dbru.zip;
    check $?;
    wget -nv https://download.oracle.com/otn_software/linux/instantclient/${ORACLE_MAJOR}${ORACLE_MINOR}000/instantclient-sdk-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0dbru.zip;
    check $?;
    unzip instantclient-sdk-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0dbru.zip;
    check $?;
    rm instantclient-sdk-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0dbru.zip;
    check $?;
    cd "${LIBS_PATH}"/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR};
    check $?;
    ln -s libclntshcore.so.${ORACLE_MAJOR}.1 libclntshcore.so;
    check $?;
}

install_protobuf() {
    echo "Install Protobuf ver: ${PROTOBUF_VERSION} into ${LIBS_PATH}/protobuf";
    if [ -d ${LIBS_PATH} ]; then
        rm -rf ${LIBS_PATH}/protobuf*;
        check $?;
    else 
        mkdir --parents --mode 777 ${LIBS_PATH};
        check $?;
    fi

    cd "${LIBS_PATH}";
    check $?;
    git clone -b v${PROTOBUF_VERSION} https://github.com/protocolbuffers/protobuf.git protobuf-${PROTOBUF_VERSION};
    check $?;
    cd protobuf-${PROTOBUF_VERSION};
    check $?;
    git submodule update --init --recursive;
    check $?;
    cmake . -DABSL_PROPAGATE_CXX_STD=ON -Dprotobuf_BUILD_SHARED_LIBS=ON;
    check $?;
    cmake --build . --parallel;
    check $?;
    cmake --install . --prefix "${LIBS_PATH}"/protobuf;
    check $?;
    rm -rf "${LIBS_PATH}"/protobuf-${PROTOBUF_VERSION};
    check $?;
}

install_replicator() {
    echo "Install OpenLogReplicator ver: ${OLR_VERSION} into ${OLR_PATH}/OpenLogReplicator";

    if [ -d ${OLR_PATH} ]; then
        rm -rf ${OLR_PATH}/OpenLogReplicator;
        check $?;
    else 
        mkdir --parents --mode 777 ${OLR_PATH};
        check $?;
    fi

    cd "${OLR_PATH}";
    check $?;
    git clone -b v${OLR_VERSION} https://github.com/Igor743646/OpenLogReplicator.git
    check $?;
}

install() {
    WITH_PACKAGES=0
    WITH_ORACLE=0
    WITH_RAPIDJSON=0
    WITH_PROTOBUF=0
    WITH_OLR=0

    while test $# -ne 0; do
        case $1 in
            --oracle) WITH_ORACLE=1;;

            --rapidjson) WITH_RAPIDJSON=1;;

            --packages) WITH_PACKAGES=1;;

            --protobuf) WITH_PROTOBUF=1;;

            --replicator) WITH_OLR=1;;

            --all) WITH_ORACLE=1; WITH_RAPIDJSON=1; WITH_PACKAGES=1; WITH_PROTOBUF=1; WITH_OLR=1;;

            --minimum) WITH_RAPIDJSON=1; WITH_PACKAGES=1; WITH_OLR=1;;

            --) shift
                break;;

            *)  echo "$0: invalid option: $1" >&2
                exit 1;;
        esac
        shift
    done
    
    echo "Packages: ${WITH_PACKAGES}";
    echo "OpenLogReplicator: ${WITH_OLR}";
    echo "RapidJson: ${WITH_RAPIDJSON}";
    echo "Protobuf: ${WITH_PROTOBUF}";
    echo "Oracle API: ${WITH_ORACLE}";
    
    if test $WITH_PACKAGES -eq 1; then
        install_packages;
        check $?;
    fi

    if test $WITH_RAPIDJSON -eq 1; then
        install_rapidjson;
        check $?;
    fi

    if test $WITH_ORACLE -eq 1; then
        install_oracle;
        check $?;
    fi

    if test $WITH_PROTOBUF -eq 1; then
        install_protobuf;
        check $?;
    fi

    if test $WITH_OLR -eq 1; then
        install_replicator;
        check $?;
    fi
}

build() {
    WITH_ORACLE="${LIBS_PATH}"/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR}
    WITH_RAPIDJSON="${LIBS_PATH}"/rapidjson
    WITH_PROTOBUF="${LIBS_PATH}"/protobuf
    MODE="Release"
    WITH_STATIC=0

    while test $# -ne 0; do
        case $1 in
            --debug) MODE="Debug";;

            --release) MODE="Release";;

            --oracle) WITH_ORACLE=$2; shift;;

            --rapidjson) WITH_RAPIDJSON=$2; shift;;

            --protobuf) WITH_PROTOBUF=$2; shift;;

            --no-protobuf) WITH_PROTOBUF="";;

            --no-oracle) WITH_ORACLE="";;

            --static) WITH_STATIC=1;;

            --) shift
                break;;

            *)  echo "$0: invalid option: $1" >&2
                exit 1;;
        esac
        shift
    done
    
    echo "Mode: ${MODE}";
    echo "WITH_RAPIDJSON: ${WITH_RAPIDJSON}";
    echo "WITH_PROTOBUF: ${WITH_PROTOBUF}";
    echo "WITH_OCI: ${WITH_ORACLE}";
    echo "WITH_STATIC: ${WITH_STATIC}";

    if [ -n "${WITH_PROTOBUF}" ]; then
        cd "${OLR_PATH}"/OpenLogReplicator/proto;
        check $?;
        "${WITH_PROTOBUF}"/bin/protoc OraProtoBuf.proto --cpp_out=.;
        check $?;
        mv OraProtoBuf.pb.cc ../src/common/OraProtoBuf.pb.cpp;
        check $?;
        mv OraProtoBuf.pb.h ../src/common/OraProtoBuf.pb.h;
        check $?;
        cd ..;
    else 
        echo "No protoc. Does not compile OraProtoBuf.proto.";
    fi
    
    rm -rf build/;

    PARAMS="-DWITH_RAPIDJSON=${WITH_RAPIDJSON} -DWITH_OCI=${WITH_ORACLE} -DWITH_PROTOBUF=${WITH_PROTOBUF} -DCMAKE_BUILD_TYPE=${MODE}"
    if [ ${WITH_STATIC} -eq 1 ]; then
        PARAMS="${PARAMS} -DWITH_STATIC=ON"
    fi

    export LD_LIBRARY_PATH="${WITH_ORACLE}:${WITH_PROTOBUF}/lib"
    cmake -S "${OLR_PATH}"/OpenLogReplicator -B "${OLR_PATH}"/OpenLogReplicator/build/ ${PARAMS};
    check $?;
    cmake --build "${OLR_PATH}"/OpenLogReplicator/build/ -j;
    check $?;
    echo ${LD_LIBRARY_PATH};

}

while test $# -ne 0; do
  case $1 in
    install) shift; install $@; exit $?;;

    build) shift; build $@; exit $?;;

    --help) echo -e "$USAGE"; exit $?;;

    --version) echo "OpenLogReplicator version: $OLR_VERSION"; exit $?;;

    --) shift
        break;;

    -*) echo "$0: invalid option: $1" >&2
        exit 1;;

    *)  break;;
  esac
  shift
done

exit;
