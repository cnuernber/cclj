; ModuleID = 'raii.cpp'
target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-f32:32:32-f64:32:64-v64:64:64-v128:128:128-a0:0:64-f80:32:32-n8:16:32-S128"
target triple = "i386-pc-linux-gnu"

%struct.incrementor = type { i32* }
%"class.std::basic_string" = type { %"struct.std::basic_string<char, std::char_traits<char>, std::allocator<char> >::_Alloc_hider" }
%"struct.std::basic_string<char, std::char_traits<char>, std::allocator<char> >::_Alloc_hider" = type { i8* }
%"class.std::allocator" = type { i8 }
%"class.std::runtime_error" = type { %"class.std::exception", %"class.std::basic_string" }
%"class.std::exception" = type { i32 (...)** }
%"class.std::logic_error" = type { %"class.std::exception", %"class.std::basic_string" }

@_ZTISt11logic_error = external constant i8*
@_ZTISt13runtime_error = external constant i8*
@.str = private unnamed_addr constant [4 x i8] c"err\00", align 1

define i32 @main(i32 %c, i8** %v) {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i8**, align 4
  %test = alloca i32, align 4
  %testinc = alloca %struct.incrementor, align 4
  %test2 = alloca %struct.incrementor, align 4
  %4 = alloca i8*
  %5 = alloca i32
  %6 = alloca %"class.std::basic_string", align 4
  %7 = alloca %"class.std::allocator", align 1
  %8 = alloca i1
  %re = alloca %"class.std::runtime_error"*, align 4
  %le = alloca %"class.std::logic_error"*, align 4
  %9 = alloca i32
  store i32 0, i32* %1
  store i32 %c, i32* %2, align 4
  store i8** %v, i8*** %3, align 4
  store i32 10, i32* %test, align 4
  call void @_ZN11incrementorC1ERi(%struct.incrementor* %testinc, i32* %test)
  %10 = load i32* %test, align 4
  %11 = add nsw i32 %10, 5
  store i32 %11, i32* %test, align 4
  %12 = load i32* %test, align 4
  %13 = srem i32 %12, 5
  %14 = icmp eq i32 %13, 0
  br i1 %14, label %15, label %75

; <label>:15                                      ; preds = %0
  invoke void @_ZN11incrementorC1ERi(%struct.incrementor* %test2, i32* %test)
          to label %16 unwind label %26

; <label>:16                                      ; preds = %15
  %17 = load i32* %test, align 4
  %18 = icmp ne i32 %17, 0
  br i1 %18, label %19, label %43

; <label>:19                                      ; preds = %16
  %20 = call i8* @__cxa_allocate_exception(i32 8) nounwind
  store i1 true, i1* %8
  %21 = bitcast i8* %20 to %"class.std::runtime_error"*
  call void @_ZNSaIcEC1Ev(%"class.std::allocator"* %7) nounwind
  invoke void @_ZNSsC1EPKcRKSaIcE(%"class.std::basic_string"* %6, i8* getelementptr inbounds ([4 x i8]* @.str, i32 0, i32 0), %"class.std::allocator"* %7)
          to label %22 unwind label %30

; <label>:22                                      ; preds = %19
  invoke void @_ZNSt13runtime_errorC1ERKSs(%"class.std::runtime_error"* %21, %"class.std::basic_string"* %6)
          to label %23 unwind label %34

; <label>:23                                      ; preds = %22
  store i1 false, i1* %8
  invoke void @__cxa_throw(i8* %20, i8* bitcast (i8** @_ZTISt13runtime_error to i8*), i8* bitcast (void (%"class.std::runtime_error"*)* @_ZNSt13runtime_errorD1Ev to i8*)) noreturn
          to label %88 unwind label %34
                                                  ; No predecessors!
  invoke void @_ZNSsD1Ev(%"class.std::basic_string"* %6)
          to label %25 unwind label %30

; <label>:25                                      ; preds = %24
  call void @_ZNSaIcED1Ev(%"class.std::allocator"* %7) nounwind
  br label %43

; <label>:26                                      ; preds = %43, %15
  %27 = landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)
          catch i8* bitcast (i8** @_ZTISt11logic_error to i8*)
          catch i8* bitcast (i8** @_ZTISt13runtime_error to i8*)
          catch i8* null
  %28 = extractvalue { i8*, i32 } %27, 0
  store i8* %28, i8** %4
  %29 = extractvalue { i8*, i32 } %27, 1
  store i32 %29, i32* %5
  br label %46

; <label>:30                                      ; preds = %24, %19
  %31 = landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)
          catch i8* bitcast (i8** @_ZTISt11logic_error to i8*)
          catch i8* bitcast (i8** @_ZTISt13runtime_error to i8*)
          catch i8* null
  %32 = extractvalue { i8*, i32 } %31, 0
  store i8* %32, i8** %4
  %33 = extractvalue { i8*, i32 } %31, 1
  store i32 %33, i32* %5
  br label %39

; <label>:34                                      ; preds = %23, %22
  %35 = landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)
          catch i8* bitcast (i8** @_ZTISt11logic_error to i8*)
          catch i8* bitcast (i8** @_ZTISt13runtime_error to i8*)
          catch i8* null
  %36 = extractvalue { i8*, i32 } %35, 0
  store i8* %36, i8** %4
  %37 = extractvalue { i8*, i32 } %35, 1
  store i32 %37, i32* %5
  invoke void @_ZNSsD1Ev(%"class.std::basic_string"* %6)
          to label %38 unwind label %86

; <label>:38                                      ; preds = %34
  br label %39

; <label>:39                                      ; preds = %38, %30
  call void @_ZNSaIcED1Ev(%"class.std::allocator"* %7) nounwind
  %40 = load i1* %8
  br i1 %40, label %41, label %42

; <label>:41                                      ; preds = %39
  call void @__cxa_free_exception(i8* %20) nounwind
  br label %42

; <label>:42                                      ; preds = %41, %39
  invoke void @_ZN11incrementorD1Ev(%struct.incrementor* %test2)
          to label %45 unwind label %86

; <label>:43                                      ; preds = %25, %16
  invoke void @_ZN11incrementorD1Ev(%struct.incrementor* %test2)
          to label %44 unwind label %26

; <label>:44                                      ; preds = %43
  br label %75

; <label>:45                                      ; preds = %42
  br label %46

; <label>:46                                      ; preds = %45, %26
  %47 = load i32* %5
  %48 = call i32 @llvm.eh.typeid.for(i8* bitcast (i8** @_ZTISt11logic_error to i8*)) nounwind
  %49 = icmp eq i32 %47, %48
  br i1 %49, label %50, label %59

; <label>:50                                      ; preds = %46
  %51 = load i8** %4
  %52 = call i8* @__cxa_begin_catch(i8* %51) nounwind
  %53 = bitcast i8* %52 to %"class.std::logic_error"*
  store %"class.std::logic_error"* %53, %"class.std::logic_error"** %le
  %54 = load i32* %test, align 4
  %55 = add nsw i32 %54, 20
  store i32 %55, i32* %test, align 4
  invoke void @__cxa_end_catch()
          to label %56 unwind label %76

; <label>:56                                      ; preds = %50
  br label %57

; <label>:57                                      ; preds = %56, %68, %74, %75
  store i32 1, i32* %1
  store i32 1, i32* %9
  call void @_ZN11incrementorD1Ev(%struct.incrementor* %testinc)
  %58 = load i32* %1
  ret i32 %58

; <label>:59                                      ; preds = %46
  %60 = call i32 @llvm.eh.typeid.for(i8* bitcast (i8** @_ZTISt13runtime_error to i8*)) nounwind
  %61 = icmp eq i32 %47, %60
  br i1 %61, label %62, label %69

; <label>:62                                      ; preds = %59
  %63 = load i8** %4
  %64 = call i8* @__cxa_begin_catch(i8* %63) nounwind
  %65 = bitcast i8* %64 to %"class.std::runtime_error"*
  store %"class.std::runtime_error"* %65, %"class.std::runtime_error"** %re
  %66 = load i32* %test, align 4
  %67 = sub nsw i32 %66, 10
  store i32 %67, i32* %test, align 4
  invoke void @__cxa_end_catch()
          to label %68 unwind label %76

; <label>:68                                      ; preds = %62
  br label %57

; <label>:69                                      ; preds = %59
  %70 = load i8** %4
  %71 = call i8* @__cxa_begin_catch(i8* %70) nounwind
  %72 = load i32* %test, align 4
  %73 = mul nsw i32 %72, 5
  store i32 %73, i32* %test, align 4
  invoke void @__cxa_end_catch()
          to label %74 unwind label %76

; <label>:74                                      ; preds = %69
  br label %57

; <label>:75                                      ; preds = %44, %0
  br label %57

; <label>:76                                      ; preds = %50, %62, %69
  %77 = landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)
          cleanup
  %78 = extractvalue { i8*, i32 } %77, 0
  store i8* %78, i8** %4
  %79 = extractvalue { i8*, i32 } %77, 1
  store i32 %79, i32* %5
  invoke void @_ZN11incrementorD1Ev(%struct.incrementor* %testinc)
          to label %80 unwind label %86

; <label>:80                                      ; preds = %76
  br label %81

; <label>:81                                      ; preds = %80
  %82 = load i8** %4
  %83 = load i32* %5
  %84 = insertvalue { i8*, i32 } undef, i8* %82, 0
  %85 = insertvalue { i8*, i32 } %84, i32 %83, 1
  resume { i8*, i32 } %85

; <label>:86                                      ; preds = %76, %42, %34
  %87 = landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)
          catch i8* null
  call void @_ZSt9terminatev() noreturn nounwind
  unreachable

; <label>:88                                      ; preds = %23
  unreachable
}

define linkonce_odr void @_ZN11incrementorC1ERi(%struct.incrementor* %this, i32* %v) unnamed_addr align 2 {
  %1 = alloca %struct.incrementor*, align 4
  %2 = alloca i32*, align 4
  store %struct.incrementor* %this, %struct.incrementor** %1, align 4
  store i32* %v, i32** %2, align 4
  %3 = load %struct.incrementor** %1
  %4 = load i32** %2
  call void @_ZN11incrementorC2ERi(%struct.incrementor* %3, i32* %4)
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare i8* @__cxa_allocate_exception(i32)

declare void @_ZNSt13runtime_errorC1ERKSs(%"class.std::runtime_error"*, %"class.std::basic_string"*)

declare void @_ZNSsC1EPKcRKSaIcE(%"class.std::basic_string"*, i8*, %"class.std::allocator"*)

declare void @_ZNSaIcEC1Ev(%"class.std::allocator"*) nounwind

declare void @_ZNSt13runtime_errorD1Ev(%"class.std::runtime_error"*) nounwind

declare void @__cxa_throw(i8*, i8*, i8*)

declare void @_ZNSsD1Ev(%"class.std::basic_string"*)

declare void @_ZSt9terminatev()

declare void @_ZNSaIcED1Ev(%"class.std::allocator"*) nounwind

declare void @__cxa_free_exception(i8*)

define linkonce_odr void @_ZN11incrementorD1Ev(%struct.incrementor* %this) unnamed_addr align 2 {
  %1 = alloca %struct.incrementor*, align 4
  store %struct.incrementor* %this, %struct.incrementor** %1, align 4
  %2 = load %struct.incrementor** %1
  call void @_ZN11incrementorD2Ev(%struct.incrementor* %2)
  ret void
}

declare i32 @llvm.eh.typeid.for(i8*) nounwind readnone

declare i8* @__cxa_begin_catch(i8*)

declare void @__cxa_end_catch()

define linkonce_odr void @_ZN11incrementorD2Ev(%struct.incrementor* %this) unnamed_addr nounwind align 2 {
  %1 = alloca %struct.incrementor*, align 4
  store %struct.incrementor* %this, %struct.incrementor** %1, align 4
  %2 = load %struct.incrementor** %1
  %3 = getelementptr inbounds %struct.incrementor* %2, i32 0, i32 0
  %4 = load i32** %3, align 4
  %5 = load i32* %4, align 4
  %6 = add nsw i32 %5, -1
  store i32 %6, i32* %4, align 4
  ret void
}

define linkonce_odr void @_ZN11incrementorC2ERi(%struct.incrementor* %this, i32* %v) unnamed_addr nounwind align 2 {
  %1 = alloca %struct.incrementor*, align 4
  %2 = alloca i32*, align 4
  store %struct.incrementor* %this, %struct.incrementor** %1, align 4
  store i32* %v, i32** %2, align 4
  %3 = load %struct.incrementor** %1
  %4 = getelementptr inbounds %struct.incrementor* %3, i32 0, i32 0
  %5 = load i32** %2, align 4
  store i32* %5, i32** %4, align 4
  ret void
}
