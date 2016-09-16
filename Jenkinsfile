#!groovy

try {
  def gitTag
  def gitSha
  def dependencies

  stage 'gather-info'
  node {
    checkout([
      $class: 'GitSCM',
      branches: scm.branches,
      gitTool: 'native git',
      extensions: scm.extensions + [[$class: 'CleanCheckout']],
      userRemoteConfigs: scm.userRemoteConfigs
    ])
    sh 'git archive -o core.zip HEAD'
    stash includes: 'core.zip', name: 'core-source'

    dependencies = readProperties file: 'dependencies.list'
    echo "VERSION: ${dependencies.VERSION}"

    gitTag = readGitTag()
    gitSha = readGitSha()
    echo "tag: ${gitTag}"
    if (gitTag == "") {
      echo "No tag given for this build"
      setBuildName(gitSha)
    } else {
      if (gitTag != "v${dependencies.VERSION}") {
        echo "Git tag '${gitTag}' does not match v${dependencies.VERSION}"
      } else {
        echo "Building release: '${gitTag}'"
        setBuildName("Tag ${gitTag}")
      }
    }
  }

  stage 'check'

  parallelExecutors = [
    checkLinuxRelease: doBuildInDocker('check'),
    checkLinuxDebug: doBuildInDocker('check-debug'),
    buildCocoa: doBuildCocoa(),
    buildNodeLinux: doBuildNodeInDocker(),
    buildNodeOsx: doBuildNodeInOsx(),
    buildDotnetOsx: doBuildDotNetOsx(),
    buildAndroid: doBuildAndroid(),
    addressSanitizer: doBuildInDocker('jenkins-pipeline-address-sanitizer')
    //threadSanitizer: doBuildInDocker('jenkins-pipeline-thread-sanitizer')
  ]

  if (env.CHANGE_TARGET) {
    parallelExecutors['diffCoverage'] = buildDiffCoverage()
  }

  parallel parallelExecutors

  stage 'build-packages'
  parallel(
    generic: doBuildPackage('generic', 'tgz'),
    centos7: doBuildPackage('centos-7', 'rpm'),
    centos6: doBuildPackage('centos-6', 'rpm'),
    ubuntu1604: doBuildPackage('ubuntu-1604', 'deb')
  )

  if (['master', 'next-major'].contains(env.BRANCH_NAME)) {
    stage 'publish-packages'
    parallel(
      generic: doPublishGeneric(),
      centos7: doPublish('centos-7', 'rpm', 'el', 7),
      centos6: doPublish('centos-6', 'rpm', 'el', 6),
      ubuntu1604: doPublish('ubuntu-1604', 'deb', 'ubuntu', 'xenial')
    )

    if (gitTag != "") {
      stage 'trigger release'
      build job: 'sync_release/realm-core-rpm-release',
        wait: false,
        parameters: [[$class: 'StringParameterValue', name: 'RPM_VERSION', value: "${dependencies.VERSION}-${env.BUILD_NUMBER}"]]
    }
  }
} catch(Exception e) {
  e.printStackTrace()
  throw e
}

def doBuildCocoa() {
  return {
    node('osx') {
      getArchive()

      try {
        withEnv([
          'PATH=$PATH:/usr/local/bin',
          'DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer',
          'REALM_ENABLE_ENCRYPTION=yes',
          'REALM_ENABLE_ASSERTIONS=yes',
          'MAKEFLAGS=\'CFLAGS_DEBUG=-Oz\'',
          'UNITTEST_SHUFFLE=1',
          'UNITTEST_REANDOM_SEED=random',
          'UNITTEST_XML=1',
          'UNITTEST_THREADS=1'
        ]) {
            sh '''
              dir=$(pwd)
              sh build.sh config $dir/install
              sh build.sh build-cocoa
              sh build.sh check-debug

              # Repack the release with just what we need so that it's not a 1 GB download
              version=$(sh build.sh get-version)
              tmpdir=$(mktemp -d /tmp/$$.XXXXXX) || exit 1
              (
                  cd $tmpdir || exit 1
                  unzip -qq "$dir/core-$version.zip" || exit 1

                  # We only need an armv7s slice for CocoaPods, and the podspec never uses
                  # the debug build of core, so remove that slice
                  lipo -remove armv7s core/librealm-ios-dbg.a -o core/librealm-ios-dbg.a

                  tar cf "$dir/core-$version.tar.xz" --xz core || exit 1
              )
              rm -rf "$tmpdir" || exit 1

              cp core-*.tar.xz realm-core-latest.tar.xz
            '''
            archive '*core-*.*.*.tar.xz'

            sh 'sh build.sh clean'
        }
      } finally {
        collectCompilerWarnings('clang')
        recordTests('check-debug-cocoa')
        withCredentials([[$class: 'FileBinding', credentialsId: 'c0cc8f9e-c3f1-4e22-b22f-6568392e26ae', variable: 's3cfg_config_file']]) {
          sh 's3cmd -c $s3cfg_config_file put realm-core-latest.tar.xz s3://static.realm.io/downloads/core'
        }
      }
    }
  }
}

def doBuildDotNetOsx() {
  return {
    node('osx') {
      getArchive()

      try {
        withEnv([
          'PATH=$PATH:/usr/local/bin',
          'DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer',
          'REALM_ENABLE_ENCRYPTION=yes',
          'REALM_ENABLE_ASSERTIONS=yes',
          'MAKEFLAGS=\'CFLAGS_DEBUG=-Oz\'',
          'UNITTEST_SHUFFLE=1',
          'UNITTEST_REANDOM_SEED=random',
          'UNITTEST_XML=1',
          'UNITTEST_THREADS=1'
        ]) {
            sh '''
              dir=$(pwd)
              sh build.sh config $dir/install
              sh build.sh build-dotnet-cocoa

              # Repack the release with just what we need so that it's not a 1 GB download
              version=$(sh build.sh get-version)
              tmpdir=$(mktemp -d /tmp/$$.XXXXXX) || exit 1
              (
                  cd $tmpdir || exit 1
                  unzip -qq "$dir/realm-core-dotnet-cocoa-$version.zip" || exit 1

                  # We only need an armv7s slice for CocoaPods, and the podspec never uses
                  # the debug build of core, so remove that slice
                  lipo -remove armv7s core/librealm-ios-no-bitcode-dbg.a -o core/librealm-ios-no-bitcode-dbg.a

                  tar cjf "$dir/realm-core-dotnet-cocoa-$version.tar.bz2" core || exit 1
              )
              rm -rf "$tmpdir" || exit 1

              cp realm-core-dotnet-cocoa-*.tar.bz2 realm-core-dotnet-cocoa-latest.tar.bz2
            '''
            archive '*core-*.*.*.tar.bz2'

            sh 'sh build.sh clean'
        }
      } finally {
        collectCompilerWarnings('clang')
        withCredentials([[$class: 'FileBinding', credentialsId: 'c0cc8f9e-c3f1-4e22-b22f-6568392e26ae', variable: 's3cfg_config_file']]) {
          sh 's3cmd -c $s3cfg_config_file put realm-core-dotnet-cocoa-latest.tar.bz2 s3://static.realm.io/downloads/core'
        }
      }
    }
  }
}


def doBuildInDocker(String command) {
  return {
    node('docker') {
      getArchive()

      def buildEnv = docker.build 'realm-core:snapshot'
      def environment = environment()
      withEnv(environment) {
        buildEnv.inside {
          sh 'sh build.sh config'
          try {
              sh "sh build.sh ${command}"
          } finally {
            collectCompilerWarnings('gcc')
            recordTests(command)
          }
        }
      }
    }
  }
}

def buildDiffCoverage() {
  return {
    node('docker') {
      checkout([
        $class: 'GitSCM',
        branches: scm.branches,
        gitTool: 'native git',
        extensions: scm.extensions + [[$class: 'CleanCheckout']],
        userRemoteConfigs: scm.userRemoteConfigs
      ])

      def buildEnv = docker.build 'realm-core:snapshot'
      def environment = environment()
      withEnv(environment) {
        buildEnv.inside {
          sh 'sh build.sh config'
          sh 'sh build.sh jenkins-pipeline-coverage'

          sh 'mkdir -p coverage'
          sh "diff-cover gcovr.xml " +
            "--compare-branch=origin/${env.CHANGE_TARGET} " +
            "--html-report coverage/diff-coverage-report.html " +
            "| grep -F Coverage: " +
            "| head -n 1 " +
            "> diff-coverage"

          publishHTML(target: [
              allowMissing: false,
              alwaysLinkToLastBuild: false,
              keepAll: true,
              reportDir: 'coverage',
              reportFiles: 'diff-coverage-report.html',
              reportName: 'Diff Coverage'
          ])

          def coverageResults = readFile('diff-coverage')

          withCredentials([[$class: 'StringBinding', credentialsId: 'bot-github-token', variable: 'githubToken']]) {
              sh "curl -H \"Authorization: token ${env.githubToken}\" " +
                 "-d '{ \"body\": \"${coverageResults}\\n\\nPlease check your coverage here: ${env.BUILD_URL}Diff_Coverage\"}' " +
                 "\"https://api.github.com/repos/realm/realm-core/issues/${env.CHANGE_ID}/comments\""
          }
        }
      }
    }
  }
}

def doBuildNodeInDocker() {
  return {
    node('docker') {
      getArchive()

      def buildEnv = docker.build 'realm-core:snapshot'
      def environment = ['REALM_ENABLE_ENCRYPTION=yes', 'REALM_ENABLE_ASSERTIONS=yes']
      withEnv(environment) {
        buildEnv.inside {
          sh 'sh build.sh config'
          try {
              sh 'sh build.sh build-node-package'
              sh 'cp realm-core-node-*.tar.gz realm-core-node-linux-latest.tar.gz'
              archive '*realm-core-node-linux-*.*.*.tar.gz'
              withCredentials([[$class: 'FileBinding', credentialsId: 'c0cc8f9e-c3f1-4e22-b22f-6568392e26ae', variable: 's3cfg_config_file']]) {
                sh 's3cmd -c $s3cfg_config_file put realm-core-node-linux-latest.tar.gz s3://static.realm.io/downloads/core'
              }
          } finally {
            collectCompilerWarnings('gcc')
          }
        }
      }
    }
  }
}

def doBuildNodeInOsx() {
  return {
    node('osx') {
      getArchive()

      def environment = ['REALM_ENABLE_ENCRYPTION=yes', 'REALM_ENABLE_ASSERTIONS=yes']
      withEnv(environment) {
        sh 'sh build.sh config'
        try {
          sh 'sh build.sh build-node-package'
          sh 'cp realm-core-node-*.tar.gz realm-core-node-osx-latest.tar.gz'
          archive '*realm-core-node-osx-*.*.*.tar.gz'

          sh 'sh build.sh clean'

          withCredentials([[$class: 'FileBinding', credentialsId: 'c0cc8f9e-c3f1-4e22-b22f-6568392e26ae', variable: 's3cfg_config_file']]) {
            sh 's3cmd -c $s3cfg_config_file put realm-core-node-osx-latest.tar.gz s3://static.realm.io/downloads/core'
          }
        } finally {
          collectCompilerWarnings('clang')
        }
      }
    }
  }
}

def doBuildAndroid() {
    def target = 'build-android'
    def buildName = "android-${target}-with-encryption"

    def environment = environment()
    environment << "REALM_ENABLE_ENCRYPTION=yes"
    environment << "PATH=/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/sbin:/bin:/usr/local/bin:/opt/android-sdk-linux/tools:/opt/android-sdk-linux/platform-tools:/opt/android-ndk-r10e"
    environment << "ANDROID_NDK_HOME=/opt/android-ndk-r10e"

    return {
        node('fastlinux') {
          ws('/tmp/core-android') {
            getArchive()

            withEnv(environment) {
              sh "sh build.sh config '${pwd()}/install'"
              sh "sh build.sh ${target}"
            }
            archive 'realm-core-android-*.tar.gz'

            dir('test/android') {
                sh '$ANDROID_HOME/tools/android update project -p . --target android-9'
                environment << "NDK_PROJECT_PATH=${pwd()}"
                withEnv(environment) {
                    dir('jni') {
                        sh "${env.ANDROID_NDK_HOME}/ndk-build V=1"
                    }
                    sh 'ant debug'
                    dir('bin') {
                        stash includes: 'NativeActivity-debug.apk', name: 'android'
                    }
                }
            }
            collectCompilerWarnings('gcc')
          }
        }

        node('android-hub') {
            sh 'rm -rf *'
            unstash 'android'

            sh 'adb devices | tee devices.txt'
            def adbDevices = readFile('devices.txt')
            def devices = getDeviceNames(adbDevices)

            if (!devices) {
                throw new IllegalStateException('No devices were found')
            }

            def device = devices[0] // Run the tests only on one device

            timeout(10) {
                sh """
                set -ex
                adb -s ${device} uninstall io.realm.coretest
                adb -s ${device} install NativeActivity-debug.apk
                adb -s ${device} logcat -c
                adb -s ${device} shell am start -a android.intent.action.MAIN -n io.realm.coretest/android.app.NativeActivity
                """

                sh """
                set -ex
                prefix="The XML file is located in "
                while [ true ]; do
                    sleep 10
                    line=\$(adb -s ${device} logcat -d -s native-activity 2>/dev/null | grep -m 1 -oE "\$prefix.*\\\$" | tr -d "\r")
                    if [ ! -z "\${line}" ]; then
                    	xml_file="\$(echo \$line | cut -d' ' -f7)"
                        adb -s ${device} pull "\$xml_file"
                        adb -s ${device} shell am force-stop io.realm.coretest
                    	break
                    fi
                done
                mkdir -p test
                cp unit-test-report.xml test/unit-test-report.xml
                """
            }
            recordTests('android-device')
        }
    }
}

def recordTests(tag) {
    def tests = readFile('test/unit-test-report.xml')
    def modifiedTests = tests.replaceAll('DefaultSuite', tag)
    writeFile file: 'test/modified-test-report.xml', text: modifiedTests

    step([
        $class: 'XUnitBuilder',
        testTimeMargin: '3000',
        thresholdMode: 1,
        thresholds: [
        [
        $class: 'FailedThreshold',
        failureNewThreshold: '0',
        failureThreshold: '0',
        unstableNewThreshold: '0',
        unstableThreshold: '0'
        ], [
        $class: 'SkippedThreshold',
        failureNewThreshold: '0',
        failureThreshold: '0',
        unstableNewThreshold: '0',
        unstableThreshold: '0'
        ]
        ],
        tools: [[
        $class: 'UnitTestJunitHudsonTestType',
        deleteOutputFiles: true,
        failIfNotNew: true,
        pattern: 'test/modified-test-report.xml',
        skipNoTestFiles: false,
        stopProcessingIfError: true
        ]]
    ])
}

def collectCompilerWarnings(compiler) {
    def parserName
    if (compiler == 'gcc') {
        parserName = 'GNU Make + GNU C Compiler (gcc)'
    } else if ( compiler == 'clang' ) {
        parserName = 'Clang (LLVM based)'
    }
    step([
        $class: 'WarningsPublisher',
        canComputeNew: false,
        canResolveRelativePaths: false,
        consoleParsers: [[parserName: parserName]],
        defaultEncoding: '',
        excludePattern: '',
        failedTotalAll: '0',
        failedTotalHigh: '0',
        failedTotalLow: '0',
        failedTotalNormal: '0',
        healthy: '',
        includePattern: '',
        messagesPattern: '',
        unHealthy: ''
    ])
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
  sh "git describe --exact-match --tags HEAD | tail -n 1 > tag.txt 2>&1 || true"
  def tag = readFile('tag.txt').trim()
  return tag
}

def readGitSha() {
  sh "git rev-parse HEAD | cut -b1-8 > sha.txt"
  def sha = readFile('sha.txt').readLines().last().trim()
  return sha
}

def get_version() {
  def dependencies = readProperties file: 'dependencies.list'
  def gitTag = readGitTag()
  def gitSha = readGitSha()
  if (gitTag == "") {
    return "${dependencies.VERSION}-g${gitSha}"
  }
  else {
    return "${dependencies.VERSION}"
  }
}

def getDeviceNames(String commandOutput) {
  def deviceNames = []
  def lines = commandOutput.split('\n')
  for (i = 0; i < lines.size(); ++i) {
    if (lines[i].contains('\t')) {
      deviceNames << lines[i].split('\t')[0].trim()
    }
  }
  return deviceNames
}

def doBuildPackage(distribution, fileType) {
  return {
    node('docker') {
      getSourceArchive()

      withCredentials([[$class: 'StringBinding', credentialsId: 'packagecloud-sync-devel-master-token', variable: 'PACKAGECLOUD_MASTER_TOKEN']]) {
        sh "sh packaging/package.sh ${distribution}"
      }

      dir('packaging/out') {
        step([$class: 'ArtifactArchiver', artifacts: "${distribution}/*.${fileType}", fingerprint: true])
        stash includes: "${distribution}/*.${fileType}", name: "packages-${distribution}"
      }
    }
  }
}

def doPublish(distribution, fileType, distroName, distroVersion) {
  return {
    node {
      getSourceArchive()
      packaging = load './packaging/publish.groovy'

      dir('packaging/out') {
        unstash "packages-${distribution}"
        dir(distribution) {
          packaging.uploadPackages('sync-devel', fileType, distroName, distroVersion, "*.${fileType}")
        }
      }
    }
  }
}

def doPublishGeneric() {
  return {
    node {
      getSourceArchive()
      def version = get_version()
      def topdir = pwd()
      dir('packaging/out') {
        unstash "packages-generic"
      }
      dir("core/v${version}/linux") {
        sh "mv ${topdir}/packaging/out/generic/realm-core-${version}.tgz realm-core-${version}.tgz"
      }

      step([
        $class: 'S3BucketPublisher',
        dontWaitForConcurrentBuildCompletion: false,
        entries: [[
          bucket: 'realm-ci-artifacts',
          excludedFile: '',
          flatten: false,
          gzipFiles: false,
          managedArtifacts: false,
          noUploadOnFailure: true,
          selectedRegion: 'us-east-1',
          sourceFile: "core/v${version}/linux/*.tgz",
          storageClass: 'STANDARD',
          uploadFromSlave: false,
          useServerSideEncryption: false
        ]],
        profileName: 'hub-jenkins-user',
        userMetadata: []
      ])
    }
  }
}

def setBuildName(newBuildName) {
  currentBuild.displayName = "${currentBuild.displayName} - ${newBuildName}"
}

def getArchive() {
    sh 'rm -rf *'
    unstash 'core-source'
    sh 'unzip -o -q core.zip'
}

def getSourceArchive() {
  checkout scm
  sh 'git clean -ffdx -e .????????'
  sh 'git submodule update --init'
}
