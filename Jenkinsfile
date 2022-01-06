#!groovy

@Library('realm-ci') _

cocoaStashes = []
androidStashes = []
publishingStashes = []

tokens = "${env.JOB_NAME}".tokenize('/')
org = tokens[tokens.size()-3]
repo = tokens[tokens.size()-2]
branch = tokens[tokens.size()-1]

jobWrapper {
  timeout(time: 5, unit: 'HOURS') {
      stage('gather-info') {
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
          }

          echo "Publishing Run: ${gitTag ? 'yes' : 'no'}"

          if (['master'].contains(env.BRANCH_NAME)) {
              // If we're on master, instruct the docker image builds to push to the
              // cache registry
              env.DOCKER_PUSH = "1"
          }
      }

      stage('check') {
          parallelExecutors = [checkLinuxRelease   : doBuildOnCentos6('Release'),
                               checkLinuxDebug     : doBuildOnCentos6('Debug'),
                               buildMacOsDebug     : doBuildMacOs('Debug'),
                               buildMacOsRelease   : doBuildMacOs('Release'),
                               buildWin32Debug     : doBuildWindows('Debug', false, 'Win32'),
                               buildWin32Release   : doBuildWindows('Release', false, 'Win32'),
                               buildWin64Debug     : doBuildWindows('Debug', false, 'x64'),
                               buildWin64Release   : doBuildWindows('Release', false, 'x64'),
                               buildUwpWin32Debug  : doBuildWindows('Debug', true, 'Win32'),
                               buildUwpWin32Release: doBuildWindows('Release', true, 'Win32'),
                               buildUwpx64Debug    : doBuildWindows('Debug', true, 'x64'),
                               buildUwpx64Release  : doBuildWindows('Release', true, 'x64'),
                               buildUwpArmDebug    : doBuildWindows('Debug', true, 'ARM'),
                               buildUwpArmRelease  : doBuildWindows('Release', true, 'ARM'),
                               threadSanitizer     : doBuildInDocker('Debug', 'thread'),
                               addressSanitizer    : doBuildInDocker('Debug', 'address'),
                               coverage            : doBuildCoverage()
              ]

          androidAbis = ['armeabi-v7a', 'x86', 'mips', 'x86_64', 'arm64-v8a']
          androidBuildTypes = ['Debug', 'MinSizeRel']

          for (def i = 0; i < androidAbis.size(); i++) {
              def abi = androidAbis[i]
              for (def j = 0; j < androidBuildTypes.size(); j++) {
                  def buildType = androidBuildTypes[j]
                  parallelExecutors["android-${abi}-${buildType}"] = doAndroidBuildInDocker(abi, buildType, abi == 'armeabi-v7a' && buildType == 'MinSizeRel')
              }
          }

          appleSdks = ['ios', 'tvos', 'watchos']
          appleBuildTypes = ['MinSizeDebug', 'Release']

          for (def i = 0; i < appleSdks.size(); i++) {
              def sdk = appleSdks[i]
              for (def j = 0; j < appleBuildTypes.size(); j++) {
                  def buildType = appleBuildTypes[j]
                  parallelExecutors["${sdk}${buildType}"] = doBuildAppleDevice(sdk, buildType)
              }
          }

          if (env.CHANGE_TARGET) {
              parallelExecutors['performance'] = buildPerformance()
          }

          parallel parallelExecutors
      }

      if (gitTag) {
          stage('Publish') {
              parallel(
                  others: doPublishLocalArtifacts()
              )
          }
      }
  }
}

def buildDockerEnv(name) {
    docker.withRegistry("https://012067661104.dkr.ecr.eu-west-1.amazonaws.com", "ecr:eu-west-1:aws-ci-user") {
        env.DOCKER_REGISTRY = '012067661104.dkr.ecr.eu-west-1.amazonaws.com'
        sh "./packaging/docker_build.sh ${name} ."
    }

    return docker.image(name)
}

def doBuildInDocker(String buildType, String sanitizeMode='') {
    return {
        node('docker') {
            getArchive()

            def buildEnv = docker.build 'realm-core:snapshot'
            def environment = environment()
            def sanitizeFlags = ''
            environment << 'UNITTEST_PROGRESS=1'
            if (sanitizeMode.contains('thread')) {
                environment << 'UNITTEST_THREADS=1'
                sanitizeFlags = '-D REALM_TSAN=ON'
            } else if (sanitizeMode.contains('address')) {
                environment << 'UNITTEST_THREADS=1'
                sanitizeFlags = '-D REALM_ASAN=ON'
            }
            withEnv(environment) {
                buildEnv.inside {
                    try {
                        sh """
                           mkdir build-dir
                           cd build-dir
                           cmake -D CMAKE_BUILD_TYPE=${buildType} ${sanitizeFlags} -G Ninja ..
                        """
                        runAndCollectWarnings(script: "cd build-dir && ninja CoreTests")
                        sh """
                           cd build-dir/test
                           ./realm-tests
                        """
                    } finally {
                        recordTests("Linux-${buildType}")
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
            def stashName = "android-${abi}___${buildType}"
            def buildDir = "build-${stashName}".replaceAll('___', '-')
            def buildEnv = docker.build('realm-core-android:snapshot', '-f android.Dockerfile .')
            def environment = environment()
            environment << 'UNITTEST_PROGRESS=1'
            withEnv(environment) {
                if(!runTestsInEmulator) {
                    buildEnv.inside {
                        runAndCollectWarnings(script: "tools/cross_compile.sh -o android -a ${abi} -t ${buildType}")
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
                            runAndCollectWarnings(script: "tools/cross_compile.sh -o android -a ${abi} -t ${buildType}")
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
                                   adb shell \'cd /data/local/tmp; ./realm-tests || echo __ADB_FAIL__\' | tee adb.log
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

def doBuildOnCentos6(String buildType) {
    return {
        node('docker') {
            getArchive()

            def stashName = "Linux___${buildType}"
            def image = docker.build('centos6:snapshot', '-f tools/docker/centos6.Dockerfile .')
            withEnv(environment()) {
                image.inside {
                    try {
                        sh """
                            mkdir build-dir
                            cd build-dir
                            cmake -D CMAKE_BUILD_TYPE=${buildType} -G Ninja ..
                            cmake --build . --target CoreTests 2>errors.log
                            cmake --build . --target check
                            cmake --build . --target package
                        """
                    } finally {
                        recordTests(stashName)
                    }
                }
            }

            dir('build-dir') {
                archiveArtifacts('realm-*.tar.gz')
                stash includes:'realm-*.tar.gz', name:stashName
                androidStashes << stashName
                if (gitTag) {
                    publishingStashes << stashName
                }
            }
        }
    }
}

def doBuildWindows(String buildType, boolean isUWP, String platform) {
    def cmakeSystemName
    def cmakeSystemVersion
    def target
    if (isUWP) {
      cmakeSystemName = 'WindowsStore'
      cmakeSystemVersion = '10.0'
      target = 'Core'
    } else {
      cmakeSystemName = 'Windows'
      cmakeSystemVersion = '8.1'
      target = 'CoreTests'
    }

    return {
        node('windows') {
            getArchive()

            dir('build-dir') {
                bat "\"${tool 'cmake'}\" -D CMAKE_SYSTEM_NAME=${cmakeSystemName} -D CMAKE_SYSTEM_VERSION=${cmakeSystemVersion} -D CMAKE_GENERATOR_PLATFORM=${platform} -D CMAKE_BUILD_TYPE=${buildType} .."
                withEnv(["_MSPDBSRV_ENDPOINT_=${UUID.randomUUID().toString()}"]) {
                    runAndCollectWarnings(parser: 'msbuild', isWindows: true, script: "\"${tool 'cmake'}\" --build . --config ${buildType} --target ${target}")
                }
                bat "\"${tool 'cmake'}\" --build . --config ${buildType} --target package"
                archiveArtifacts('*.tar.gz')
                if (gitTag) {
                    def stashName = "${cmakeSystemName}-${platform}___${buildType}"
                    stash includes:'*.tar.gz', name:stashName
                    publishingStashes << stashName
                }
            }
            if(!isUWP) {
                def environment = environment() << "TMP=${env.WORKSPACE}\\temp"
                environment << 'UNITTEST_PROGRESS=1'
                withEnv(environment) {
                    dir("build-dir/test/${buildType}") {
                        bat '''
                          mkdir %TMP%
                          realm-tests.exe --no-error-exit-code
                          rmdir /Q /S %TMP%
                        '''
                    }
                }
                recordTests("Windows-${platform}-${buildType}")
            }
        }
    }
}

def buildDiffCoverage() {
    return {
        node('docker') {
            getArchive()

            def buildEnv = buildDockerEnv('ci/realm-core:snapshot')
            def environment = environment()
            environment << 'UNITTEST_PROGRESS=1'
            withEnv(environment) {
                buildEnv.inside {
                    sh '''
                        mkdir build-dir
                        cd build-dir
                        cmake -D CMAKE_BUILD_TYPE=Debug \
                              -D REALM_COVERAGE=ON \
                              -G Ninja ..
                        ninja realm-tests
                        cd test
                        ./realm-tests
                        gcovr --filter=\'.*src/realm.*\' -x >gcovr.xml
                        mkdir coverage
                     '''
                    def coverageResults = sh(returnStdout: true, script: """
                        diff-cover build-dir/test/gcovr.xml \\
                                   --compare-branch=origin/${env.CHANGE_TARGET} \\
                                   --html-report build-dir/test/coverage/diff-coverage-report.html \\
                                   | grep Coverage: | head -n 1 > diff-coverage
                    """).trim()

                    publishHTML(target: [
                                  allowMissing         : false,
                                         alwaysLinkToLastBuild: false,
                                         keepAll              : true,
                                         reportDir            : 'build-dir/test/coverage',
                                         reportFiles          : 'diff-coverage-report.html',
                                         reportName           : 'Diff Coverage'
                                    ])

                    withCredentials([[$class: 'StringBinding', credentialsId: 'bot-github-token', variable: 'githubToken']]) {
                        sh """
                           curl -H \"Authorization: token ${env.githubToken}\" \\
                                -d '{ \"body\": \"${coverageResults}\\n\\nPlease check your coverage here: ${env.BUILD_URL}Diff_Coverage\"}' \\
                                \"https://api.github.com/repos/realm/${repo}/issues/${env.CHANGE_ID}/comments\"
                        """
                    }
                }
            }
        }
    }
}

def buildPerformance() {
  return {
    // Select docker-cph-X.  We want docker, metal (brix) and only one executor
    // (exclusive), if the machine changes also change REALM_BENCH_MACHID below
    node('docker && brix && exclusive') {
      getArchive()

      def buildEnv = buildDockerEnv('ci/realm-core:snapshot')
      // REALM_BENCH_DIR tells the gen_bench_hist.sh script where to place results
      // REALM_BENCH_MACHID gives the results an id - results are organized by hardware to prevent mixing cached results with runs on different machines
      // MPLCONFIGDIR gives the python matplotlib library a config directory, otherwise it will try to make one on the user home dir which fails in docker
      buildEnv.inside {
        withEnv(["REALM_BENCH_DIR=${env.WORKSPACE}/test/bench/core-benchmarks", "REALM_BENCH_MACHID=docker-brix","MPLCONFIGDIR=${env.WORKSPACE}/test/bench/config"]) {
          rlmS3Get file: 'core-benchmarks.zip', path: 'downloads/core/core-benchmarks.zip'
          sh 'unzip core-benchmarks.zip -d test/bench/'
          sh 'rm core-benchmarks.zip'

          sh """
            cd test/bench
            mkdir -p core-benchmarks results
            ./gen_bench_hist.sh origin/${env.CHANGE_TARGET}
            ./parse_bench_hist.py --local-html results/ core-benchmarks/
          """
          zip dir: 'test/bench', glob: 'core-benchmarks/**/*', zipFile: 'core-benchmarks.zip'
          rlmS3Put file: 'core-benchmarks.zip', path: 'downloads/core/core-benchmarks.zip'
          publishHTML(target: [allowMissing: false, alwaysLinkToLastBuild: false, keepAll: true, reportDir: 'test/bench/results', reportFiles: 'report.html', reportName: 'Performance Report'])
          withCredentials([[$class: 'StringBinding', credentialsId: 'bot-github-token', variable: 'githubToken']]) {
              sh "curl -H \"Authorization: token ${env.githubToken}\" " +
                 "-d '{ \"body\": \"Check the performance result here: ${env.BUILD_URL}Performance_Report\"}' " +
                 "\"https://api.github.com/repos/realm/${repo}/issues/${env.CHANGE_ID}/comments\""
          }
        }
      }
    }
  }
}

def doBuildMacOs(String buildType) {
    def sdk = 'macosx'
    return {
        node('osx') {
            getArchive()

            dir("build-macos-${buildType}") {
                withEnv(['DEVELOPER_DIR=/Applications/Xcode-8.2.app/Contents/Developer/']) {
                    // This is a dirty trick to work around a bug in xcode
                    // It will hang if launched on the same project (cmake trying the compiler out)
                    // in parallel.
                    retry(3) {
                        timeout(time: 2, unit: 'MINUTES') {
                            sh """
                                    rm -rf *
                                    cmake -D CMAKE_TOOLCHAIN_FILE=../tools/cmake/macos.toolchain.cmake \\
                                          -D CMAKE_BUILD_TYPE=${buildType} \\
                                          -G Xcode ..
                                """
                        }
                    }

                    runAndCollectWarnings(parser: 'clang', script: """
                            xcodebuild -sdk macosx \\
                                       -configuration ${buildType} \\
                                       -target package \\
                                       ONLY_ACTIVE_ARCH=NO
                            """)
                }
                archiveArtifacts("*.tar.gz")
            }
            def stashName = "macos___${buildType}"
            stash includes:"build-macos-${buildType}/*.tar.gz", name:stashName
            cocoaStashes << stashName
            publishingStashes << stashName
        }
    }
}

def doBuildAppleDevice(String sdk, String buildType) {
    return {
        node('osx') {
            getArchive()

            withEnv(['DEVELOPER_DIR=/Applications/Xcode-8.2.app/Contents/Developer/']) {
                retry(3) {
                    timeout(time: 15, unit: 'MINUTES') {
                        runAndCollectWarnings(parser:'clang', script: """
                                rm -rf build-*
                                tools/cross_compile.sh -o ${sdk} -t ${buildType}
                            """)
                    }
                }
            }
            dir("build-${sdk}-${buildType}") {
                archiveArtifacts("*.tar.gz")
            }
            def stashName = "${sdk}___${buildType}"
            stash includes:"build-${sdk}-${buildType}/*.tar.gz", name:stashName
            cocoaStashes << stashName
            if(gitTag) {
                publishingStashes << stashName
            }
        }
    }
}

def doBuildCoverage() {
  return {
    node('docker') {
      getArchive()
      docker.build('realm-core:snapshot').inside {
        def workspace = pwd()
        sh """
          mkdir build
          cd build
          cmake -G Ninja -D REALM_COVERAGE=ON ..
          ninja CoreTests
          cd ..
          lcov --no-external --capture --initial --directory . --output-file ${workspace}/coverage-base.info
          cd build/test
          ./realm-tests
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
    junit 'build-dir/test/modified-test-report.xml'
}

def environment() {
    return [
        "REALM_MAX_BPNODE_SIZE_DEBUG=4",
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
                for(publishingStash in publishingStashes) {
                    unstash name: publishingStash
                    def files = findFiles(glob: '**')
                    for (file in files) {
                        rlmS3Put file: file.path, path: "downloads/core/${gitDescribeVersion}/${file.name}"
                        rlmS3Put file: file.path, path: "downloads/core/${file.name}"
                    }
                    deleteDir()
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
          extensions       : scm.extensions + [[$class: 'CleanCheckout'], [$class: 'CloneOption', depth: 0, noTags: false, reference: '', shallow: false]],
          userRemoteConfigs: scm.userRemoteConfigs
        ]
    )
}
