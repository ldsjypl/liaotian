plugins {
    id("com.android.application")
}

android {
    namespace = "online.ldsjypl.chat"
    compileSdk = 36

    defaultConfig {
        applicationId = "online.ldsjypl.chat"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "1.1"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // debug signing keeps the APK directly installable
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

dependencies {
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
}
