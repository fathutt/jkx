#ifndef SHADER_PBR_GLSL
#define SHADER_PBR_GLSL

#if defined(PER_PIXEL_LIGHTING)
	float WrapLambert( in float NL, in float w )
	{
		return clamp((NL + w) / pow(1.0 + w, 2.0), 0.0, 1.0);
	}

	vec3 Diffuse_Lambert(in vec3 DiffuseColor)
	{
		return DiffuseColor * (1.0 / M_PI);
	}

	vec3 CalcNormal( in vec3 vertexNormal, in vec4 vertexTangent, in vec2 frag_tex_coord )
	{
		if ( normal_texture_set > -1 ) {
			vec3 biTangent = vertexTangent.w * cross(vertexNormal, vertexTangent.xyz);
			vec3 n = texture(normal_texture, frag_tex_coord).agb - vec3(0.5);

			n.xy *= u_global.normalScale.xy;
			n.z = sqrt(clamp((0.25 - n.x * n.x) - n.y * n.y, 0.0, 1.0));
			n = n.x * vertexTangent.xyz + n.y * biTangent + n.z * vertexNormal;

			return normalize(n);
		}
	
		else
			return normalize(vertexNormal);
	}

	vec3 CalcDiffuse( in vec3 diffuse, in float NE, in float NL,
		in float LH, in float roughness )
	{
		return Diffuse_Lambert(diffuse);
	}

	vec3 F_Schlick( in vec3 SpecularColor, in float VH )
	{
		float Fc = pow(1 - VH, 5);
		return clamp(50.0 * SpecularColor.g, 0.0, 1.0) * Fc + (1 - Fc) * SpecularColor; //hacky way to decide if reflectivity is too low (< 2%)
	}

	float D_GGX( in float NH, in float a )
	{
		float a2 = a * a;
		float d = (NH * a2 - NH) * NH + 1;
		return a2 / (M_PI * d * d);
	}

	// Appoximation of joint Smith term for GGX
	// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
	float V_SmithJointApprox( in float a, in float NV, in float NL )
	{
		float Vis_SmithV = NL * (NV * (1 - a) + a);
		float Vis_SmithL = NV * (NL * (1 - a) + a);

		// The denominator goes to zero, and the reciprocal of zero is what made
		// every physically-based world surface in this renderer BLACK.
		//
		// NL is zero on any surface facing away from the light, which on a
		// lightmapped world is most of them. Then Vis_SmithV is zero, Vis_SmithL
		// is NV*a, and when the roughness or the view term is small enough that
		// the sum underflows, this returned an infinity. CalcSpecular multiplies
		// it by D and F, and INFINITY TIMES ZERO IS NaN - the same arithmetic
		// that produced the overexposed menu models, one function further down.
		//
		// From there the poison travels: reflectance = Fd + Fs is NaN, the first
		// term of the lighting is NaN, and adding the ambient term to a NaN
		// leaves a NaN, which is written to an unsigned normalised target as
		// zero. Black.
		//
		// Measured on the fixture, and it is worth writing the numbers down
		// because four debug views appeared to contradict each other for a week:
		//
		//   Fd            (81, 81, 81)     the Lambert term, correct
		//   Fs            black            NOT zero - NaN, which writes as black
		//   reflectance   black            81 + NaN
		//   ambient*diff  (185, 63, 63)    the term that should have survived
		//   shaded        black            because NaN + anything is NaN
		//
		// isnan(Fs) confirmed it directly: the squares came back pure red on a
		// probe that painted NaN red, Inf green.
		//
		// The floor is on the denominator rather than on the result because the
		// quantity is a visibility term: at grazing angles it is legitimately
		// large, and clamping the answer would darken cases that are correct.
		return 0.5 / max( Vis_SmithV + Vis_SmithL, 1e-5 );
	}

	vec3 CalcSpecular( in vec3 specular, in float NH, in float NL,
		in float NE, float LH, in float VH, in float roughness )
	{
		vec3  F = F_Schlick(specular, VH);
		float D = D_GGX(NH, roughness);
		float V = V_SmithJointApprox(roughness, NE, NL);

		return D * F * V;
	}

	vec3 CalcIBLContribution( in float roughness, in vec3 N, in vec3 E,
		in float NE, in vec3 specular )
	{
		vec3 R = reflect(-E, N);
		R.y *= -1.0f;
	
		vec3 cubeLightColor = textureLod(env_texture, R, roughness * 6).rgb * 1.0;
		vec2 EnvBRDF = texture(brdflut_texture, vec2(NE, 1.0 - roughness)).rg;

		return cubeLightColor * (specular.rgb * EnvBRDF.x + EnvBRDF.y);
	}

	#ifdef VK_DLIGHT_GPU
		float CalcLightAttenuation(float normDist)
		{
			// zero light at 1.0, approximating q3 style
			float attenuation = 0.5 * normDist - 0.5;
			return clamp(attenuation, 0.0, 1.0);
		}

		vec3 CalcDynamicLightContribution(
			in float roughness,
			in vec3 N,
			in vec3 E,
			in vec3 viewOrigin,
			in vec3 viewDir,
			in float NE,
			in vec3 diffuse,
			in vec3 specular,
			in vec3 vertexNormal )
		{
			vec3 outColor = vec3(0.0);
			vec3 position = viewOrigin - viewDir;

			for ( int i = 0; i < u_lights.num_lights; i++ )
			{
				#if 0
					if ( ( u_LightMask & ( 1 << i ) ) == 0 ) {
						continue;
					}
				#endif

				vkUniformLightEntry_t light = u_lights.light[i];

				vec3  L  = light.origin.xyz - position;
				float sqrLightDist = dot(L, L);

				// sunny, skip if light is out of radius
				float radiusSqr = light.radius * light.radius;
				if (sqrLightDist > radiusSqr) {
					continue;
				}

				float attenuation = CalcLightAttenuation(light.radius * light.radius / sqrLightDist);

				// sunny, skip lights with negligible intensity
				if (attenuation < 0.001) {
					continue;
				}

				L /= sqrt(sqrLightDist);

				vec3  H  = normalize(L + E);
				float NL = clamp(dot(N, L), 0.0, 1.0);
				float LH = clamp(dot(L, H), 0.0, 1.0);
				float NH = clamp(dot(N, H), 0.0, 1.0);
				float VH = clamp(dot(E, H), 0.0, 1.0);

				vec3 reflectance = diffuse + CalcSpecular(specular, NH, NL, NE, LH, VH, roughness);

				outColor += light.color * reflectance * attenuation * NL;
			}

			return outColor;
		}
	#endif
#endif

#endif