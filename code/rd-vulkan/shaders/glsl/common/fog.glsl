#ifndef SHADER_FOG_GLSL
#define SHADER_FOG_GLSL

#if defined(USE_FOG) || defined(USE_FOG_EXP)
	#if defined(USE_FOG)
		void ApplyACFF(inout vec4 base, in vec4 fogColor, in float fogAlpha)
		{
			const int acff = 0;	// not implemented in pbr branch :(

			if ( acff == 1 )		// ACFF_RGB
				base.rgb *= ( 1.0 - fogAlpha );        

			else if ( acff == 2 )		// ACFF_RGBA
				base *= ( 1.0 - fogAlpha );            

			else if ( acff == 3 )		// ACFF_ALPHA
				base.a *= ( 1.0 - fogAlpha );         

			else			// default: ACFF_NONE
				base = mix( base, fogColor, fogAlpha ); 
		}
	#endif

	vec4 CalcFog(in vec3 viewOrigin, in vec3 position, in vkUniformFogEntry_t fog)
	{
		bool inFog = dot(viewOrigin, fog.plane.xyz) - fog.plane.w >= 0.0 || (fog.hasPlane == 0);

		// line: x = o + tv
		// plane: (x . n) + d = 0
		// intersects: dot(o + tv, n) + d = 0
		//             dot(o + tv, n) = -d
		//             dot(o, n) + t*dot(n, v) = -d
		//             t = -(d + dot(o, n)) / dot(n, v)
		vec3 V = position - viewOrigin;

		// fogPlane is inverted in tr_bsp for some reason.
		float t = -(fog.plane.w + dot(viewOrigin, -fog.plane.xyz)) / dot(V, -fog.plane.xyz);

		// only use this for objects with potentially two contibuting fogs
		#if defined(USE_FALLBACK_GLOBAL_FOG)
			bool intersects = (t > 0.0 && t < 0.995);
			if (inFog == intersects)
			{
				int u_globalFogIndex = int(fogDistanceVector[0]);
				Fog globalFog = u_fogs.fogs[u_globalFogIndex];

				float distToVertex = length(V);
				float distFromIntersection = distToVertex - (t * distToVertex);
				float z = globalFog.depthToOpaque * mix(distToVertex, distFromIntersection, intersects);
				return vec4(globalFog.color.rgb, 1.0 - clamp(exp(-(z * z)), 0.0, 1.0));
			}
		#else
			bool intersects = (t > 0.0 && t < 0.995);
			if (inFog == intersects)
				return vec4(0.0);
		#endif

		float distToVertexFromViewOrigin = length(V);
		float distToIntersectionFromViewOrigin = t * distToVertexFromViewOrigin;

		float distOutsideFog = max(distToVertexFromViewOrigin - distToIntersectionFromViewOrigin, 0.0);
		float distThroughFog = mix(distOutsideFog, distToVertexFromViewOrigin, inFog);

		float z = fog.depthToOpaque * distThroughFog;
		return vec4(fog.color.rgb * fogDistanceVector[2], 1.0 - clamp(exp(-(z * z)), 0.0, 1.0));
	}
#endif

#if defined(USE_VOLUMETRIC_FOG)
	// The same fog, coloured by the light that actually reaches it.
	//
	// WHAT THIS DOES NOT CHANGE: how much fog there is. The alpha below is the
	// same 1 - exp(-(z*z)) the ordinary path returns, computed from the same
	// distance through the same brush. Only the colour moves. That is a
	// deliberate line, and it is what makes the effect measurable: switch the
	// cvar and the amount of fog in the frame is identical to the bit, while its
	// colour follows the map's lighting. An effect that changed both at once
	// could only be argued about.
	//
	// The weights are not invented either. Splitting the segment into N steps,
	// the transmittance at distance d is T(d) = exp(-(k*d)^2), and the light
	// picked up between two steps is weighted by how much the transmittance fell
	// across them - T(d_i) - T(d_i+1). Those differences sum to exactly
	// 1 - T(total), which is the alpha, so dividing by the alpha at the end
	// gives a weighted MEAN of the light along the ray. A corridor lit evenly at
	// full brightness therefore reproduces the old colour exactly, and every
	// darker place is darker by the amount the map says it is.
	vec3 SampleLightVolume(in vec3 worldPos)
	{
		vec3 uvw = (worldPos - u_fogs.lightGridOrigin.xyz) * u_fogs.lightGridScale.xyz;
		return texture(u_lightVolume, uvw).rgb;
	}

	vec4 CalcVolumetricFog(in vec3 viewOrigin, in vec3 position, in vkUniformFogEntry_t fog)
	{
		vec4 flat_fog = CalcFog(viewOrigin, position, fog);

		int steps = int(u_fogs.lightGridScale.w);

		// No volume on this map, or nothing to march through. The second case is
		// not laziness: a fragment the ordinary path decided is unfogged has no
		// segment inside the brush, and marching a zero-length segment divides
		// by zero rather than returning nothing.
		if (steps < 1 || flat_fog.a <= 0.0)
			return flat_fog;

		vec3 V = position - viewOrigin;
		float distToVertex = length(V);
		if (distToVertex <= 0.0)
			return flat_fog;

		vec3 dir = V / distToVertex;

		bool inFog = dot(viewOrigin, fog.plane.xyz) - fog.plane.w >= 0.0 || (fog.hasPlane == 0);
		float t = -(fog.plane.w + dot(viewOrigin, -fog.plane.xyz)) / dot(V, -fog.plane.xyz);
		float distToIntersection = t * distToVertex;

		// Where the segment inside the brush begins: at the eye when the eye is
		// in the fog, at the surface of the brush otherwise.
		float start = inFog ? 0.0 : max(distToIntersection, 0.0);
		float span  = max(distToVertex - start, 0.0);

		if (span <= 0.0)
			return flat_fog;

		vec3 lit = vec3(0.0);
		float prevT = 1.0;
		float invSteps = 1.0 / float(steps);

		for (int i = 1; i <= steps; i++)
		{
			float d = span * float(i) * invSteps;
			float zi = fog.depthToOpaque * d;
			float T = exp(-(zi * zi));
			float dT = max(prevT - T, 0.0);

			// The middle of the step, not its end: the light picked up between
			// two samples belongs to the space between them.
			lit += SampleLightVolume(viewOrigin + dir * (start + d - span * invSteps * 0.5)) * dT;
			prevT = T;
		}

		float weight = 1.0 - prevT;
		if (weight <= 0.0)
			return flat_fog;

		return vec4(flat_fog.rgb * (lit / weight) * u_fogs.lightGridOrigin.w, flat_fog.a);
	}
#endif

#endif