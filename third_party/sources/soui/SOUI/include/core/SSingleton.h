/**
* Copyright (C) 2014-2050 
* All rights reserved.
* 
* @file       SSingleton.h
* @brief      
* @version    v1.0      
* @author     SOUI group   
* @date       2014/08/02
* 
* Describe    SOUI系统中使用的单件模块
*/

#pragma once

#include <assert.h>

namespace SOUI
{

    /**
    * @class      SSingleton
    * @brief      单件模板
    * 
    * Describe    
    */
    template <typename T> 
    class SSingleton
    {
    protected:
        static    T* ms_Singleton;

    public:
        SSingleton( void )
        {
            assert( !ms_Singleton );
            ms_Singleton = static_cast<T*>(this);
        }
        virtual ~SSingleton( void )
        {
            assert( ms_Singleton );
            ms_Singleton = 0;
        }
        static T& getSingleton( void )
        {
            assert( ms_Singleton );
            return ( *ms_Singleton );
        }
        static T* getSingletonPtr( void )
        {
            return ( ms_Singleton );
        }

    private:
        SSingleton& operator=(const SSingleton&)
        {
            return *this;
        }
        SSingleton(const SSingleton&) {}
    };

#if defined(__clang__)
    // MSVC retains its original implicit instantiation/export behavior.
    // These specializations have one definition in their existing cpp files.
    // Declare them before exported classes instantiate inherited members.
    class SApplication;
    class SNotifyCenter;
    class SFontPool;
    class SWindowFinder;
    class SScriptTimer;
    template<> SOUI_EXP SApplication* SSingleton<SApplication>::ms_Singleton;
    template<> SOUI_EXP SNotifyCenter* SSingleton<SNotifyCenter>::ms_Singleton;
    template<> SFontPool* SSingleton<SFontPool>::ms_Singleton;
    template<> SWindowFinder* SSingleton<SWindowFinder>::ms_Singleton;
    template<> SScriptTimer* SSingleton<SScriptTimer>::ms_Singleton;
#endif
}//namespace SOUI
