plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// 签名配置从 keystore.properties (已 gitignore) 读取，缺失时回退到 debug 签名。
// 注意：要与游戏同 uid 读内存，App 必须用和重打包游戏相同的 keystore 签名。
// 复制 keystore.properties.example -> keystore.properties 并填入你自己的 keystore。
val keystorePropsFile = rootProject.file("keystore.properties")
val hasKeystore = keystorePropsFile.exists()

// 不用 java.util.Properties：它会把 Windows 路径里的 \ 当转义吃掉（D:\keys -> D:keys）。
// 按行朴素解析，原样保留反斜杠，取用时统一换成 /。
val keystoreProps: Map<String, String> = if (hasKeystore) {
    keystorePropsFile.readLines()
        .map { it.trim() }
        .filter { it.isNotEmpty() && !it.startsWith("#") && it.contains('=') }
        .associate { line ->
            val i = line.indexOf('=')
            line.substring(0, i).trim() to line.substring(i + 1).trim()
        }
} else emptyMap()

fun prop(name: String): String = keystoreProps[name].orEmpty().replace('\\', '/')

// storeFile 允许绝对路径，也允许相对项目根目录
fun resolveStoreFile(): File {
    val raw = prop("storeFile")
    val f = File(raw)
    return if (f.isAbsolute) f else rootProject.file(raw)
}

android {
    namespace = "com.openrubi.luciatools"
    compileSdk = 34
    // 固定 NDK 版本，避免 AGP 自动下载另一份（体积 1GB+）
    ndkVersion = "26.1.10909125"

    signingConfigs {
        if (hasKeystore) {
            val ksFile = resolveStoreFile()
            if (!ksFile.exists()) {
                throw GradleException(
                    "keystore.properties 里的 storeFile 不存在：${ksFile.absolutePath}\n" +
                        "请把已验证过的旧 keystore 放到该路径，或修改 storeFile。"
                )
            }
            create("lucia") {
                storeFile = ksFile
                storePassword = prop("storePassword")
                keyAlias = prop("keyAlias")
                keyPassword = prop("keyPassword")
                // 游戏包 v2/v3 签名，工具必须同样开 v2 才能同 uid
                enableV1Signing = false
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    defaultConfig {
        applicationId = "com.openrubi.luciatools"
        minSdk = 24
        // sharedUserId 在高 targetSdk 仍可用(仅 deprecated 警告)；保持与游戏可同 uid
        targetSdk = 30
        versionCode = 1
        versionName = "1.0"
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_STL=c++_static"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        debug {
            if (hasKeystore) signingConfig = signingConfigs.getByName("lucia")
        }
        release {
            isMinifyEnabled = false
            if (hasKeystore) signingConfig = signingConfigs.getByName("lucia")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        viewBinding = true
    }

    lint {
        // targetSdk 30 是刻意选择：sharedUserId 在低 target 下才能稳定与游戏同 uid 读内存。
        // 本项目不上架 Google Play，屏蔽该检查，否则 release 构建会被 lintVital 直接拦死。
        disable += "ExpiredTargetSdkVersion"
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}
