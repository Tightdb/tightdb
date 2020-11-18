#!groovy

@Library('realm-ci') _

cocoaStashes = []
androidStashes = []
publishingStashes = []
dependencies = null

tokens = "${env.JOB_NAME}".tokenize('/')
org = tokens[tokens.size()-3]
repo = tokens[tokens.size()-2]
branch = tokens[tokens.size()-1]

jobWrapper {
    stage('gather-info') {
        isPullRequest = !!env.CHANGE_TARGET
        targetBranch = isPullRequest ? env.CHANGE_TARGET : "none"
        node('docker') {
            getSourceArchive()
            stash includes: '**', name: 'core-source', useDefaultExcludes: false

            dependencies = readProperties file: 'dependencies.list'
            echo "Version in dependencies.list: ${dependencies.VERSION}"

            gitTag = readGitTag()
            gitSha = sh(returnStdout: true, script: 'git rev-parse HEAD').trim().take(8)
            gitDescribeVersion = sh(returnStdout: true, script: 'git describe --tags').trim()

            echo "Git tag: ${gitTag ?: 'none'}"
            if (!gitTag) {
                echo "No tag given for this build"
                setBuildName(gitSha)
            } else {
                if (gitTag != "v${dependencies.VERSION}") {
                    error "Git tag '${gitTag}' does not match v${dependencies.VERSION}"
                } else {
                    echo "Building release: '${gitTag}'"
                    setBuildName("Tag ${gitTag}")
                }
            }
            targetSHA1 = 'NONE'
            if (isPullRequest) {
                targetSHA1 = sh(returnStdout: true, script: "git fetch origin && git merge-base origin/${targetBranch} HEAD").trim()
            }
        }

        currentBranch = env.BRANCH_NAME
        println "Building branch: ${currentBranch}"
        println "Target branch: ${targetBranch}"

        releaseTesting = targetBranch.contains('release')
        isMaster = currentBranch.contains('master')
        longRunningTests = isMaster || currentBranch.contains('next-major')
        isPublishingRun = false
        if (gitTag) {
            isPublishingRun = currentBranch.contains('release')
        }

        echo "Pull request: ${isPullRequest ? 'yes' : 'no'}"
        echo "Release Run: ${releaseTesting ? 'yes' : 'no'}"
        echo "Publishing Run: ${isPublishingRun ? 'yes' : 'no'}"
        echo "Long running test: ${longRunningTests ? 'yes' : 'no'}"

        if (isMaster) {
            // If we're on master, instruct the docker image builds to push to the
            // cache registry
            env.DOCKER_PUSH = "1"
        }
    }

    if (isPullRequest) {
        stage('FormatCheck') {
            node('docker') {
                getArchive()
                docker.build('realm-core-clang:snapshot', '-f clang.Dockerfile .').inside() {
                    echo "Checking code formatting"
                    modifications = sh(returnStdout: true, script: "git clang-format --diff ${targetSHA1}").trim()
                    try {
                        if (!modifications.equals('no modified files to format')) {
                            if (!modifications.equals('clang-format did not modify any files')) {
                                echo "Commit violates formatting rules"
                                sh "git clang-format --diff ${targetSHA1} > format_error.txt"
                                archiveArtifacts('format_error.txt')
                                sh 'exit 1'
                            }
                        }
                        currentBuild.result = 'SUCCESS'
                    } catch (Exception err) {
                        currentBuild.result = 'FAILURE'
                        throw err
                    }
                }
            }
        }
    }

    stage('Checking') {
        def buildOptions = [
            buildType : "Debug",
            maxBpNodeSize: "1000",
            enableEncryption: "ON",
            enableSync: "OFF",
            runTests: true,
        ]
        def linuxOptionsNoEncrypt = [
            buildType : "Debug",
            maxBpNodeSize: "4",
            enableEncryption: "OFF",
            enableSync: "OFF",
        ]
        def armhfQemuTestOptions = [
            emulator: 'LD_LIBRARY_PATH=/usr/arm-linux-gnueabihf/lib qemu-arm -cpu cortex-a7',
        ]
        def armhfNativeTestOptions = [
            nativeNode: 'docker-arm',
            nativeDocker: 'armhf-native.Dockerfile',
            nativeDockerPlatform: 'linux/arm/v7',
        ]

        parallelExecutors = [
            checkLinuxDebug         : doCheckInDocker(buildOptions),
            checkLinuxRelease_4     : doCheckInDocker(buildOptions + [maxBpNodeSize: "4", buildType : "Release"]),
            checkLinuxDebug_Sync    : doCheckInDocker(buildOptions + [enableSync : "ON"]),
            checkLinuxDebugNoEncryp : doCheckInDocker(buildOptions + [enableEncryption: "OFF"]),
            checkMacOsRelease_Sync  : doBuildMacOs(buildOptions + [buildType : "Release", enableSync : "ON"]),
            checkWindows_x86_Release: doBuildWindows('Release', false, 'Win32', true),
            checkWindows_x64_Debug  : doBuildWindows('Debug', false, 'x64', true),
            buildUWP_x86_Release    : doBuildWindows('Release', true, 'Win32', false),
            buildUWP_ARM_Debug      : doBuildWindows('Debug', true, 'ARM', false),
            buildiosDebug           : doBuildAppleDevice('iphoneos', 'MinSizeDebug'),
            buildandroidArm64Debug  : doAndroidBuildInDocker('arm64-v8a', 'Debug', false),
            checkRaspberryPiQemu    : doLinuxCrossCompile('armhf', 'Debug', armhfQemuTestOptions),
            checkRaspberryPiNative  : doLinuxCrossCompile('armhf', 'Debug', armhfNativeTestOptions),
            threadSanitizer         : doCheckSanity(buildOptions + [enableSync : "ON", sanitizeMode : "thread"]),
            addressSanitizer        : doCheckSanity(buildOptions + [enableSync : "ON", sanitizeMode : "address"]),
            performance             : optionalBuildPerformance(releaseTesting), // always build performance on releases, otherwise make it optional
        ]
        if (releaseTesting) {
            extendedChecks = [
                checkRaspberryPiQemuRelease   : doLinuxCrossCompile('armhf', 'Release', armhfQemuTestOptions),
                checkRaspberryPiNativeRelease : doLinuxCrossCompile('armhf', 'Release', armhfNativeTestOptions),
                checkMacOsDebug               : doBuildMacOs('Debug', true),
                buildUWP_x64_Debug            : doBuildWindows('Debug', true, 'x64', false),
                androidArmeabiRelease         : doAndroidBuildInDocker('armeabi-v7a', 'Release', true),
                coverage                      : doBuildCoverage(),
                // valgrind                : doCheckValgrind()
            ]
            parallelExecutors.putAll(extendedChecks)
        }
        parallel parallelExecutors
    }

    if (isPublishingRun) {
        stage('BuildPackages') {
            def buildOptions = [
                enableSync: "ON",
                runTests: false,
            ]

            parallelExecutors = [
                buildMacOsDebug     : doBuildMacOs(buildOptions + [buildType : "MinSizeDebug"]),
                buildMacOsRelease   : doBuildMacOs(buildOptions + [buildType : "Release"]),
                buildCatalystDebug  : doBuildMacOsCatalyst('MinSizeDebug'),
                buildCatalystRelease: doBuildMacOsCatalyst('Release'),

                buildLinuxASAN      : doBuildLinuxClang("RelASAN"),
                buildLinuxTSAN      : doBuildLinuxClang("RelTSAN")
            ]

            androidAbis = ['armeabi-v7a', 'x86', 'x86_64', 'arm64-v8a']
            androidBuildTypes = ['Debug', 'Release']

            for (abi in androidAbis) {
                for (buildType in androidBuildTypes) {
                    parallelExecutors["android-${abi}-${buildType}"] = doAndroidBuildInDocker(abi, buildType, false)
                }
            }

            appleSdks = ['iphoneos', 'iphonesimulator',
                         'appletvos', 'appletvsimulator',
                         'watchos', 'watchsimulator']
            appleBuildTypes = ['MinSizeDebug', 'Release']

            for (sdk in appleSdks) {
                for (buildType in appleBuildTypes) {
                    parallelExecutors["${sdk}${buildType}"] = doBuildAppleDevice(sdk, buildType)
                }
            }

            linuxBuildTypes = ['Debug', 'Release', 'RelAssert']
            linuxCrossCompileTargets = ['armhf']

            for (buildType in linuxBuildTypes) {
                parallelExecutors["buildLinux${buildType}"] = doBuildLinux(buildType)
                for (target in linuxCrossCompileTargets) {
                    parallelExecutors["crossCompileLinux-${target}-${buildType}"] = doLinuxCrossCompile(target, buildType)
                }
            }

            windowsBuildTypes = ['Debug', 'Release']
            windowsPlatforms = ['Win32', 'x64']

            for (buildType in windowsBuildTypes) {
                for (platform in windowsPlatforms) {
                    parallelExecutors["buildWindows-${platform}-${buildType}"] = doBuildWindows(buildType, false, platform, false)
                    parallelExecutors["buildWindowsUniversal-${platform}-${buildType}"] = doBuildWindows(buildType, true, platform, false)
                }
                parallelExecutors["buildWindowsUniversal-ARM-${buildType}"] = doBuildWindows(buildType, true, 'ARM', false)
            }

            parallel parallelExecutors
        }
        stage('Aggregate') {
            parallel (
                cocoa: {
                    node('osx') {
                        getArchive()
                        for (cocoaStash in cocoaStashes) {
                            unstash name: cocoaStash
                        }
                        sh 'tools/build-cocoa.sh -x'
                        archiveArtifacts('realm-*-cocoa*.tar.gz')
                        archiveArtifacts('realm-*-cocoa*.tar.xz')
                        stash includes: 'realm-*-cocoa*.tar.xz', name: "cocoa-xz"
                        stash includes: 'realm-*-cocoa*.tar.gz', name: "cocoa-gz"
                        publishingStashes << "cocoa-xz"
                        publishingStashes << "cocoa-gz"
                    }
                },
                android: {
                    node('docker') {
                        getArchive()
                        for (androidStash in androidStashes) {
                            unstash name: androidStash
                        }
                        sh 'tools/build-android.sh'
                        archiveArtifacts('realm-core-android*.tar.gz')
                        def stashName = 'android'
                        stash includes: 'realm-core-android*.tar.gz', name: stashName
                        publishingStashes << stashName
                    }
                }
            )
        }
        stage('publish-packages') {
            parallel(
                others: doPublishLocalArtifacts()
            )
        }
    }
}

def doCheckInDocker(Map options = [:]) {
    def cmakeOptions = [
        CMAKE_BUILD_TYPE: options.buildType,
        REALM_MAX_BPNODE_SIZE: options.maxBpNodeSize,
        REALM_ENABLE_ENCRYPTION: options.enableEncryption,
        REALM_ENABLE_SYNC: options.enableSync,
    ]
    if (options.enableSync == "ON") {
        cmakeOptions << [
            REALM_ENABLE_AUTH_TESTS: "ON",
            REALM_MONGODB_ENDPOINT: "http://mongodb-realm:9090",
        ]
    }
    if (longRunningTests) {
        cmakeOptions << [
            CMAKE_CXX_FLAGS: '"-DTEST_DURATION=1"',
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        node('docker') {
            getArchive()
            def sourcesDir = pwd()
            def buildEnv = docker.build 'realm-core-linux:18.04'
            def environment = environment()
            environment << 'UNITTEST_PROGRESS=1'

            cmakeDefinitions += " -DREALM_STITCH_CONFIG=\"${sourcesDir}/test/object-store/mongodb/stitch.json\""

            def buildSteps = { String dockerArgs = "" ->
                withEnv(environment) {
                    buildEnv.inside("${dockerArgs}") {
                        try {
                            dir('build-dir') {
                                sh "cmake ${cmakeDefinitions} -G Ninja .."
                                runAndCollectWarnings(script: "ninja", name: "linux-${options.buildType}-encrypt${options.enableEncryption}-BPNODESIZE_${options.maxBpNodeSize}")
                                sh 'ctest --output-on-failure'
                            }
                        } finally {
                            recordTests("Linux-${options.buildType}")
                        }
                    }
                }
            }
            
            if (options.enableSync == "ON") {
                // stitch images are auto-published every day to our CI
                // see https://github.com/realm/ci/tree/master/realm/docker/mongodb-realm
                // we refrain from using "latest" here to optimise docker pull cost due to a new image being built every day
                // if there's really a new feature you need from the latest stitch, upgrade this manually
                withRealmCloud(version: dependencies.MDBREALM_TEST_SERVER_TAG, appsToImport: ['auth-integration-tests': "${env.WORKSPACE}/test/object-store/mongodb"]) { networkName ->
                    buildSteps("--network=${networkName}")
                }
            } else {
                buildSteps("")
            }
        }
    }
}

def doCheckSanity(Map options = [:]) {
    def privileged = '';

    def cmakeOptions = [
        CMAKE_BUILD_TYPE: options.buildType,
        REALM_MAX_BPNODE_SIZE: options.maxBpNodeSize,
        REALM_ENABLE_SYNC: options.enableSync,
    ]

    if (options.sanitizeMode.contains('thread')) {
        cmakeOptions << [
            REALM_TSAN: "ON",
        ]
    }
    else if (options.sanitizeMode.contains('address')) {
        privileged = '--privileged'
        cmakeOptions << [
            REALM_ASAN: "ON",
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        node('docker') {
            getArchive()
            def buildEnv = docker.build('realm-core-linux:clang', '-f clang.Dockerfile .')
            def environment = environment()
            environment << 'UNITTEST_PROGRESS=1'
            withEnv(environment) {
                buildEnv.inside(privileged) {
                    try {
                        dir('build-dir') {
                            sh "cmake ${cmakeDefinitions} -G Ninja .."
                            runAndCollectWarnings(script: "ninja", parser: "clang", name: "linux-clang-${options.buildType}-${options.sanitizeMode}")
                            sh 'ctest --output-on-failure'
                        }

                    } finally {
                        recordTests("Linux-${options.buildType}")
                    }
                }
            }
        }
    }
}

def doBuildLinux(String buildType) {
    return {
        node('docker') {
            getSourceArchive()

            docker.build('realm-core-generic:gcc-8', '-f generic.Dockerfile .').inside {
                sh """
                   rm -rf build-dir
                   mkdir build-dir
                   cd build-dir
                   scl enable devtoolset-8 -- cmake -DCMAKE_BUILD_TYPE=${buildType} -DREALM_NO_TESTS=1 -G Ninja ..
                   ninja
                   cpack -G TGZ
                """
            }

            dir('build-dir') {
                archiveArtifacts("*.tar.gz")
                def stashName = "linux___${buildType}"
                stash includes:"*.tar.gz", name:stashName
                publishingStashes << stashName
            }
        }
    }
}

def doBuildLinuxClang(String buildType) {
    return {
        node('docker') {
            getArchive()
            docker.build('realm-core-linux:clang', '-f clang.Dockerfile .').inside() {
                dir('build-dir') {
                    sh "cmake -D CMAKE_BUILD_TYPE=${buildType} -DREALM_NO_TESTS=1 -G Ninja .."
                    runAndCollectWarnings(script: "ninja", parser: "clang", name: "linux-clang-${buildType}")
                    sh 'cpack -G TGZ'
                }
            }
            dir('build-dir') {
                archiveArtifacts("*.tar.gz")
                def stashName = "linux___${buildType}"
                stash includes:"*.tar.gz", name:stashName
                publishingStashes << stashName
            }
        }
    }
}

def doCheckValgrind() {
    return {
        node('docker') {
            getArchive()
            def buildEnv = docker.build 'realm-core-linux:18.04'
            def environment = environment()
            environment << 'UNITTEST_PROGRESS=1'
            withEnv(environment) {
                buildEnv.inside {
                    def workspace = pwd()
                    try {
                        sh """
                           mkdir build-dir
                           cd build-dir
                           cmake -D CMAKE_BUILD_TYPE=RelWithDebInfo -D REALM_VALGRIND=ON -D REALM_ENABLE_ALLOC_SET_ZERO=ON -D REALM_MAX_BPNODE_SIZE=1000 -G Ninja ..
                        """
                        runAndCollectWarnings(script: "cd build-dir && ninja", name: "linux-valgrind")
                        sh """
                            cd build-dir/test
                            valgrind --version
                            valgrind --tool=memcheck --leak-check=full --undef-value-errors=yes --track-origins=yes --child-silent-after-fork=no --trace-children=yes --suppressions=${workspace}/test/valgrind.suppress --error-exitcode=1 ./realm-tests --no-error-exitcode
                        """
                    } finally {
                        recordTests("Linux-ValgrindDebug")
                    }
                }
            }
        }
    }
}

def doAndroidBuildInDocker(String abi, String buildType, boolean runTestsInEmulator) {
    def cores = 4
    return {
        node('docker') {
            getArchive()
            def stashName = "android___${abi}___${buildType}"
            def buildDir = "build-${stashName}".replaceAll('___', '-')
            def buildEnv = docker.build('realm-core-android:ndk21', '-f android.Dockerfile .')
            def environment = environment()
            environment << 'UNITTEST_PROGRESS=1'
            withEnv(environment) {
                if(!runTestsInEmulator) {
                    buildEnv.inside {
                        sh "tools/cross_compile.sh -o android -a ${abi} -t ${buildType} -v ${gitDescribeVersion} -f -DREALM_NO_TESTS=1"
                        dir(buildDir) {
                            archiveArtifacts('realm-*.tar.gz')
                        }
                        stash includes:"${buildDir}/realm-*.tar.gz", name:stashName
                        androidStashes << stashName
                        if (gitTag) {
                            publishingStashes << stashName
                        }
                    }
                } else {
                    docker.image('tracer0tong/android-emulator').withRun('-e ARCH=armeabi-v7a') { emulator ->
                        buildEnv.inside("--link ${emulator.id}:emulator") {
                            runAndCollectWarnings(script: "tools/cross_compile.sh -o android -a ${abi} -t ${buildType} -v ${gitDescribeVersion} -f -DREALM_ENABLE_SYNC=0", name: "android-armeabi-${abi}-${buildType}")
                            dir(buildDir) {
                                archiveArtifacts('realm-*.tar.gz')
                            }
                            stash includes:"${buildDir}/realm-*.tar.gz", name:stashName
                            androidStashes << stashName
                            if (gitTag) {
                                publishingStashes << stashName
                            }
                            try {
                                sh '''
                                   cd $(find . -type d -maxdepth 1 -name build-android*)
                                   adb connect emulator
                                   timeout 10m adb wait-for-device
                                   adb push test/realm-tests /data/local/tmp
                                   find test -type f -name "*.json" -maxdepth 1 -exec adb push {} /data/local/tmp \\;
                                   find test -type f -name "*.realm" -maxdepth 1 -exec adb push {} /data/local/tmp \\;
                                   find test -type f -name "*.txt" -maxdepth 1 -exec adb push {} /data/local/tmp \\;
                                   adb shell \'cd /data/local/tmp; UNITTEST_PROGRESS=1 ./realm-tests || echo __ADB_FAIL__\' | tee adb.log
                                   ! grep __ADB_FAIL__ adb.log
                               '''
                            } finally {
                                sh '''
                                   mkdir -p build-dir/test
                                   cd build-dir/test
                                   adb pull /data/local/tmp/unit-test-report.xml
                                '''
                                recordTests('android')
                            }
                        }
                    }
                }
            }
        }
    }
}

def doBuildWindows(String buildType, boolean isUWP, String platform, boolean runTests) {
    def warningFilters = [];
    def cpackSystemName = "${isUWP ? 'UWP' : 'Windows'}-${platform}"
    def arch = platform.toLowerCase()
    if (arch == 'win32') {
      arch = 'x86'
    }
    if (arch == 'win64') {
      arch = 'x64'
    }
    def triplet = "${arch}-${isUWP ? 'uwp' : 'windows'}-static"

    def cmakeOptions = [
      CMAKE_GENERATOR_PLATFORM: platform,
      CMAKE_BUILD_TYPE: buildType,
      REALM_ENABLE_SYNC: "ON",
      CPACK_SYSTEM_NAME: cpackSystemName,
      CMAKE_TOOLCHAIN_FILE: "c:\\src\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake",
      VCPKG_TARGET_TRIPLET: triplet,
    ]

     if (isUWP) {
      cmakeOptions << [
        CMAKE_SYSTEM_NAME: 'WindowsStore',
        CMAKE_SYSTEM_VERSION: '10.0',
      ]
      warningFilters = [excludeMessage('Publisher name .* does not match signing certificate subject')]
    } else {
      cmakeOptions << [
        CMAKE_SYSTEM_VERSION: '8.1',
      ]
    }
    if (!runTests) {
      cmakeOptions << [
        REALM_NO_TESTS: '1',
      ]
    }
    
    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        node('windows') {
            getArchive()

            dir('build-dir') {
                bat "\"${tool 'cmake'}\" ${cmakeDefinitions} .."
                withEnv(["_MSPDBSRV_ENDPOINT_=${UUID.randomUUID().toString()}"]) {
                    runAndCollectWarnings(
                        parser: 'msbuild',
                        isWindows: true,
                        script: "\"${tool 'cmake'}\" --build . --config ${buildType}",
                        name: "windows-${platform}-${buildType}-${isUWP?'uwp':'nouwp'}",
                        filters: warningFilters
                    )
                }
                bat "\"${tool 'cmake'}\\..\\cpack.exe\" -C ${buildType} -D CPACK_GENERATOR=TGZ"
                archiveArtifacts('*.tar.gz')
                if (gitTag) {
                    def stashName = "windows___${platform}___${isUWP?'uwp':'nouwp'}___${buildType}"
                    stash includes:'*.tar.gz', name:stashName
                    publishingStashes << stashName
                }
            }
            if (runTests && !isUWP) {
                def environment = environment() << "TMP=${env.WORKSPACE}\\temp"
                environment << 'UNITTEST_PROGRESS=1'
                withEnv(environment) {
                    dir("build-dir/test/${buildType}") {
                        bat '''
                          mkdir %TMP%
                          realm-tests.exe --no-error-exit-code
                          realm-sync-tests.exe --no-error-exit-code
                          copy unit-test-report.xml ..
                          rmdir /Q /S %TMP%
                        '''
                    }
                }
                recordTests("Windows-${platform}-${buildType}")
            }
        }
    }
}

def optionalBuildPerformance(boolean force) {
    if (force) {
        return {
            buildPerformance()
        }
    } else {
        return {
            def doPerformance = true
            stage("Input") {
                try {
                    timeout(time: 10, unit: 'MINUTES') {
                        script {
                            input message: 'Build Performance?', ok: 'Yes'
                        }
                    }
                } catch (err) { // manual abort or timeout
                    println "Not building performance on this run: ${err}"
                    doPerformance = false
                }
            }
            if (doPerformance) {
                stage("Build") {
                    buildPerformance()
                }
            }
        }
    }
}

def buildPerformance() {
    // Select docker-cph-X.  We want docker, metal (brix) and only one executor
    // (exclusive), if the machine changes also change REALM_BENCH_MACHID below
    node('brix && exclusive') {
      getArchive()

      // REALM_BENCH_DIR tells the gen_bench_hist.sh script where to place results
      // REALM_BENCH_MACHID gives the results an id - results are organized by hardware to prevent mixing cached results with runs on different machines
      // MPLCONFIGDIR gives the python matplotlib library a config directory, otherwise it will try to make one on the user home dir which fails in docker
      docker.build('realm-core-linux:18.04').inside {
        withEnv(["REALM_BENCH_DIR=${env.WORKSPACE}/test/bench/core-benchmarks", "REALM_BENCH_MACHID=docker-brix","MPLCONFIGDIR=${env.WORKSPACE}/test/bench/config"]) {
          rlmS3Get file: 'core-benchmarks.zip', path: 'downloads/core/core-benchmarks.zip'
          sh 'unzip core-benchmarks.zip -d test/bench/'
          sh 'rm core-benchmarks.zip'

          sh """
            cd test/bench
            mkdir -p core-benchmarks results
            ./gen_bench_hist.sh origin/${env.CHANGE_TARGET}
          """
          zip dir: 'test/bench', glob: 'core-benchmarks/**/*', zipFile: 'core-benchmarks.zip'
          rlmS3Put file: 'core-benchmarks.zip', path: 'downloads/core/core-benchmarks.zip'
          sh 'cd test/bench && ./parse_bench_hist.py --local-html results/ core-benchmarks/'
          publishHTML(target: [allowMissing: false, alwaysLinkToLastBuild: false, keepAll: true, reportDir: 'test/bench/results', reportFiles: 'report.html', reportName: 'Performance_Report'])
          withCredentials([[$class: 'StringBinding', credentialsId: 'bot-github-token', variable: 'githubToken']]) {
              sh "curl -H \"Authorization: token ${env.githubToken}\" " +
                 "-d '{ \"body\": \"Check the performance result [here](${env.BUILD_URL}Performance_5fReport).\"}' " +
                 "\"https://api.github.com/repos/realm/${repo}/issues/${env.CHANGE_ID}/comments\""
          }
        }
      }
    }
}

def doBuildMacOs(Map options = [:]) {
    def buildType = options.buildType;
    def sdk = 'macosx'

    def cmakeOptions = [
        CMAKE_BUILD_TYPE: options.buildType,
        CMAKE_TOOLCHAIN_FILE: "../tools/cmake/macosx.toolchain.cmake",
        REALM_ENABLE_SYNC: options.enableSync,
        OSX_ARM64: 'ON',
    ]
    if (!options.runTests) {
        cmakeOptions << [
            REALM_NO_TESTS: "ON",
        ]
    }
    if (longRunningTests) {
        cmakeOptions << [
            CMAKE_CXX_FLAGS: '"-DTEST_DURATION=1"',
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        node('osx') {
            getArchive()

            dir("build-macosx-${buildType}") {
                withEnv(['DEVELOPER_DIR=/Applications/Xcode-12.2.app/Contents/Developer/']) {
                    // This is a dirty trick to work around a bug in xcode
                    // It will hang if launched on the same project (cmake trying the compiler out)
                    // in parallel.
                    retry(3) {
                        timeout(time: 2, unit: 'MINUTES') {
                            sh """
                                rm -rf *
                                cmake ${cmakeDefinitions} -D REALM_VERSION=${gitDescribeVersion} -G Ninja ..
                            """
                        }
                    }

                    runAndCollectWarnings(parser: 'clang', script: 'ninja package', name: "osx-clang-${buildType}")
                }
            }
            withEnv(['DEVELOPER_DIR=/Applications/Xcode-12.app/Contents/Developer/']) {
                runAndCollectWarnings(parser: 'clang', script: 'xcrun swift build', name: "osx-clang-xcrun-swift-${buildType}")
                sh 'xcrun swift run ObjectStoreTests'
            }

            archiveArtifacts("build-macosx-${buildType}/*.tar.gz")

            def stashName = "macosx___${buildType}"
            stash includes:"build-macosx-${buildType}/*.tar.gz", name:stashName
            cocoaStashes << stashName
            publishingStashes << stashName

            if (options.runTests) {
                try {
                    def environment = environment()
                    environment << 'UNITTEST_PROGRESS=1'
                    environment << 'CTEST_OUTPUT_ON_FAILURE=1'
                    dir("build-macosx-${buildType}") {
                        withEnv(environment) {
                            sh 'ctest'
                        }
                    }
                } finally {
                    // recordTests expects the test results xml file in a build-dir/test/ folder
                    sh """
                        mkdir -p build-dir/test
                        cp build-macosx-${buildType}/test/unit-test-report.xml build-dir/test/
                    """
                    recordTests("macosx_${buildType}")
                }
            }
        }
    }
}

def doBuildMacOsCatalyst(String buildType) {
    return {
        node('osx') {
            getArchive()

            dir("build-maccatalyst-${buildType}") {
                withEnv(['DEVELOPER_DIR=/Applications/Xcode-12.2.app/Contents/Developer/']) {
                    sh """
                            rm -rf *
                            cmake -D CMAKE_TOOLCHAIN_FILE=../tools/cmake/maccatalyst.toolchain.cmake \\
                                  -D CMAKE_BUILD_TYPE=${buildType} \\
                                  -D REALM_VERSION=${gitDescribeVersion} \\
                                  -D REALM_SKIP_SHARED_LIB=ON \\
                                  -D REALM_BUILD_LIB_ONLY=ON \\
                                  -D OSX_ARM64=1 \\
                                  -G Ninja ..
                        """
                    runAndCollectWarnings(parser: 'clang', script: 'ninja package', name: "osx-maccatalyst-${buildType}")
                }
            }

            archiveArtifacts("build-maccatalyst-${buildType}/*.tar.gz")

            def stashName = "maccatalyst__${buildType}"
            stash includes:"build-maccatalyst-${buildType}/*.tar.gz", name:stashName
            cocoaStashes << stashName
            publishingStashes << stashName
        }
    }
}

def doBuildAppleDevice(String sdk, String buildType) {
    return {
        node('osx') {
            getArchive()

            // Builds for Apple devices have to be done with the oldest Xcode
            // version we support because bitcode is not backwards-compatible.
            // This doesn't apply to simulators, and Xcode 12 supports more
            // architectures than 11, so we want to use 12 for simulator builds.
            def xcodeVersion = sdk.contains('simulator') ? '12' : '11'

            withEnv(["DEVELOPER_DIR=/Applications/Xcode-${xcodeVersion}.app/Contents/Developer/"]) {
                retry(3) {
                    timeout(time: 45, unit: 'MINUTES') {
                        sh """
                            rm -rf build-*
                            tools/cross_compile.sh -o ${sdk} -t ${buildType} -v ${gitDescribeVersion}
                        """
                    }
                }
            }
            archiveArtifacts("build-${sdk}-${buildType}/*.tar.gz")
            def stashName = "${sdk}___${buildType}"
            stash includes:"build-${sdk}-${buildType}/*.tar.gz", name:stashName
            cocoaStashes << stashName
            if(gitTag) {
                publishingStashes << stashName
            }
        }
    }
}

def doLinuxCrossCompile(String target, String buildType, Map testOptions = null) {
    def runTests = { emulated ->
            def runner = emulated ? testOptions.emulator : ''
            try {
                def environment = environment()
                environment << 'UNITTEST_PROGRESS=1'
                if (emulated) {
                    environment << 'UNITTEST_FILTER=- Thread_RobustMutex*'  // robust mutexes can't work under qemu
                }
                withEnv(environment) {
                    sh """
                        cd test
                        ulimit -s 256 # launching thousands of threads in 32-bit address space requires smaller stacks
                        ${runner} ./realm-tests
                    """
                }
            } finally {
                dir('..') {
                    def suffix = emulated ? '-emulated' : ''
                    recordTests("Linux-${target}-${buildType}${suffix}")
                }
            }
    }
    return {
        node('docker') {
            getArchive()
            docker.build("realm-core-crosscompiling:${target}", "-f ${target}.Dockerfile .").inside {
                dir('build-dir') {
                    sh """
                        cmake -GNinja \
                            -DREALM_SKIP_SHARED_LIB=ON \
                            -DCMAKE_TOOLCHAIN_FILE=$WORKSPACE/tools/cmake/${target}.toolchain.cmake \
                            -DCMAKE_BUILD_TYPE=${buildType} \
                            -DREALM_NO_TESTS=${testOptions ? 'OFF' : 'ON'} \
                            -DCPACK_SYSTEM_NAME=Linux-${target} \
                            ..
                    """

                    runAndCollectWarnings(script: "ninja", name: "linux-x_compile-${target}-${buildType}")

                    if (testOptions != null) {
                        if (testOptions.get('emulator')) {
                            runTests(true)
                        }
                        if (testOptions.get('nativeNode')) {
                            stash includes: 'test/**/*', name: "realm-tests-Linux-${target}"
                        }
                    } else {
                        sh 'cpack'
                        archiveArtifacts '*.tar.gz'
                        def stashName = "linux-${target}___${buildType}"
                        stash includes:"*.tar.gz", name:stashName
                        publishingStashes << stashName
                    }
                }
            }

            if (testOptions != null && testOptions.get('nativeNode')) {
                node(testOptions.nativeNode) {
                    getArchive()
                    docker.build("realm-core-native:${target}", "-f ${testOptions.nativeDocker} --platform ${testOptions.nativeDockerPlatform} .").inside {
                        dir('build-dir') {
                            unstash "realm-tests-Linux-${target}"
                            runTests(false)
                        }
                    }
                }
            }
        }
    }
}

def doBuildCoverage() {
  return {
    node('docker') {
      getArchive()
      docker.build('realm-core-linux:18.04').inside {
        def workspace = pwd()
        sh """
          mkdir build
          cd build
          cmake -G Ninja -D REALM_COVERAGE=ON ..
          ninja
          cd ..
          lcov --no-external --capture --initial --directory . --output-file ${workspace}/coverage-base.info
          cd build/test
          ulimit -c unlimited
          UNITTEST_PROGRESS=1 ./realm-tests
          cd ../..
          lcov --no-external --directory . --capture --output-file ${workspace}/coverage-test.info
          lcov --add-tracefile ${workspace}/coverage-base.info --add-tracefile coverage-test.info --output-file ${workspace}/coverage-total.info
          lcov --remove ${workspace}/coverage-total.info '/usr/*' '${workspace}/test/*' --output-file ${workspace}/coverage-filtered.info
          rm coverage-base.info coverage-test.info coverage-total.info
        """
        withCredentials([[$class: 'StringBinding', credentialsId: 'codecov-token-core', variable: 'CODECOV_TOKEN']]) {
          sh '''
            curl -s https://codecov.io/bash | bash
          '''
        }
      }
    }
  }
}

/**
 *  Wraps the test recorder by adding a tag which will make the test distinguishible
 */
def recordTests(tag) {
    def tests = readFile('build-dir/test/unit-test-report.xml')
    def modifiedTests = tests.replaceAll('realm-core-tests', tag)
    writeFile file: 'build-dir/test/modified-test-report.xml', text: modifiedTests
    junit testResults: 'build-dir/test/modified-test-report.xml'
}

def environment() {
    return [
        "UNITTEST_SHUFFLE=1",
        "UNITTEST_RANDOM_SEED=random",
        "UNITTEST_THREADS=1",
        "UNITTEST_XML=1"
        ]
}

def readGitTag() {
    def command = 'git describe --exact-match --tags HEAD'
    def returnStatus = sh(returnStatus: true, script: command)
    if (returnStatus != 0) {
        return null
    }
    return sh(returnStdout: true, script: command).trim()
}

def doPublishLocalArtifacts() {
    return {
        node('docker') {
            deleteDir()
            dir('temp') {
                withAWS(credentials: 'aws-credentials', region: 'us-east-1') {
                    for (publishingStash in publishingStashes) {
                        unstash name: publishingStash
                        def path = publishingStash.replaceAll('___', '/')
                        def files = findFiles(glob: '**')
                        for (file in files) {
                            rlmS3Put file: file.path, path: "downloads/core/${gitDescribeVersion}/${path}/${file.name}"
                            rlmS3Put file: file.path, path: "downloads/core/${file.name}"
                        }
                        deleteDir()
                    }
                }
            }
        }
    }
}

def setBuildName(newBuildName) {
    currentBuild.displayName = "${currentBuild.displayName} - ${newBuildName}"
}

def getArchive() {
    deleteDir()
    unstash 'core-source'
}

def getSourceArchive() {
    checkout(
        [
          $class           : 'GitSCM',
          branches         : scm.branches,
          gitTool          : 'native git',
          extensions       : scm.extensions + [[$class: 'CleanCheckout'], [$class: 'CloneOption', depth: 0, noTags: false, reference: '', shallow: false],
                                               [$class: 'SubmoduleOption', disableSubmodules: false, parentCredentials: false, recursiveSubmodules: true,
                                                         reference: '', trackingSubmodules: false]],
          userRemoteConfigs: scm.userRemoteConfigs
        ]
    )
}
